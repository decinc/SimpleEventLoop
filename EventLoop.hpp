#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <stdlib.h>
#include <cstring>
#include <iostream>
#include <functional>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <algorithm>
#include <iterator>

#ifndef __EVENT_LOOP_H
#define __EVENT_LOOP_H
typedef struct EventData{
  std::shared_ptr<char> data;
  int len;
}EventData;
struct cmp_str{
  bool operator()(const char* a, const char* b) const{
    
    return std::strcmp(a, b) < 0;
  }
};
typedef std::function<void(void*)> EVENT;
typedef std::function<void(void*, int)> EVENTDATAHANDLER;

class EventLoop {
private:
  std::map<const char*, std::vector<EVENT>, cmp_str> eventMap;
  std::map<const char*, std::vector<EVENTDATAHANDLER>, cmp_str> onceMap;
  int ep_fd;
  int pipe_fd[2];

  pthread_mutex_t event_fd_lock;

  void _write(void* content, int len){
    if(len == 0) return;
    write(pipe_fd[1], content, len);
  }
  void _read(void* buffer, int len){
    if(len == 0) return;
    int read_len = read(pipe_fd[0], buffer, len);
    if(read_len < 0) return;
    if(read_len != len){
      int remain_len = len - read_len;
      _read(((char*)buffer) + read_len, remain_len);
    }
  }

public:
  EventLoop(){
    pipe(pipe_fd);
    pthread_mutex_init(&event_fd_lock, NULL);
  }
  void loop(){
    EventLoop* loop = this;
    pthread_t event_thread;
    pthread_create(&event_thread, NULL, EventLoop::_runEventLoop, (void*)loop);
  }
  static void* _runEventLoop(void* _this){
    ((EventLoop*)_this)->runEventLoop();
    return NULL;
  }
  void runEventLoop(){
    int event_cnt = 0;
    while(1){
    
      int event_name_len;
      char event_name[256] = {0, };
      int paramLength;
      void* param;
      _read((void*)&event_name_len, 4);
      _read((void*)event_name, event_name_len);
      if(!hasEvent(event_name)){
        throw std::string("NOT REGISTERED EVENT : ") + event_name;
      }
      _read((void*)&paramLength, 4);
      if(paramLength){
        param = malloc(paramLength);
        _read(param, paramLength);
        handleEvent(event_name, param);
        handleData(event_name, param, paramLength);
        free(param);
      }else{
        handleEvent(event_name, NULL);
      }
    }
  
  }
  void emit(const char* eventName, void* param, int paramLength){
    int event_name_len = strlen(eventName);
    pthread_mutex_lock(&event_fd_lock);
    _write((void*)&event_name_len, 4);
    _write((void*)eventName, event_name_len);
    _write((void*)&paramLength, 4);
    if(paramLength) _write((void*)param, paramLength);
    pthread_mutex_unlock(&event_fd_lock);
  }
  void handleEvent(char* eventName, void* param){
    if(eventMap.count(eventName) == 0){
      return;
    }
    auto v = eventMap[eventName];
    for(auto it = v.begin(); it != v.end(); it++) (*it)(param);
  }
  void registerEvent(const char* eventName, EVENT handler){
    if(eventMap.count(eventName) == 0){
      eventMap[eventName] = std::vector<EVENT>();
    }
    eventMap[eventName].push_back(handler);
  }
  bool hasEvent(const char* eventName){
    return eventMap.count(eventName) != 0 || onceMap.count(eventName) != 0;
  }

  void registerEventOnce(const char* eventName, EVENTDATAHANDLER handler){
    if(onceMap.count(eventName) == 0){
      onceMap[eventName] = std::vector<EVENTDATAHANDLER>();
    }
    onceMap[eventName].push_back(handler);
  }

  EventData waitData(const char* channelName){
    pthread_mutex_t lock;
    pthread_mutex_init(&lock, NULL);
    pthread_mutex_lock(&lock);
    EventData d;
    registerEventOnce(channelName, [&lock, &d](void* data, int dataLength){
      d.len = dataLength;
      char* x = new char[dataLength];
      std::copy_n((char*)data, dataLength, x);

      std::shared_ptr<char> ptr(x, std::default_delete<char[]>());
      d.data = ptr;

      pthread_mutex_unlock(&lock);
    });
    pthread_mutex_lock(&lock);
    pthread_mutex_unlock(&lock);
    pthread_mutex_destroy(&lock);
    return d;
  }
  void fireData(const char* channelName, void* data, int dataLength){
    emit(channelName, data, dataLength);
  }
  void handleData(const char* channelName, void* data, int dataLength){
    if(onceMap.count(channelName) == 0){
      return;
    }
    auto v = onceMap[channelName];
    for(auto it = v.begin(); it != v.end(); it++) (*it)(data, dataLength);
    v.clear();
  }

};

/**
 * usage
 * EventLoop loop; // create loop
 * loop.registerEvent("eventName", std::function(void(void*))); // add event handler for "eventName"
 * loop.loop(); // start event loop
 * loop.emit("eventName", param); // fire event for "eventName"
 * 
 * loop.fireData("channelName", data, len); // send data with length len
 * loop.waitData("channelName"); // wait EventData with channel "channelName"
 * **/
#endif