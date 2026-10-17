#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
using std::max;
using namespace std::chrono_literals;

// Exercise the real worker and leave() while a decoder call is still in flight.
// Fake FreeRTOS primitives use real host threads, rather than synchronous mocks.
struct Semaphore {
    std::recursive_mutex lock;
    std::mutex state;
    std::condition_variable changed;
    bool ready=false;
};
struct Task {std::atomic<bool> notified{false};};
using SemaphoreHandle_t=Semaphore*;
using TaskHandle_t=Task*;
static thread_local Task *currentTask=nullptr;
static std::mutex state;
static std::condition_variable changed;
static bool entered=false,allowFinish=false;
static std::atomic<bool> inDecoder{false},deleted{false},unmounted{false},restored{false};
static constexpr int portMAX_DELAY=0,pdTRUE=1,LOW=0;
static void xSemaphoreTakeRecursive(Semaphore *s,int){s->lock.lock();}
static void xSemaphoreGiveRecursive(Semaphore *s){s->lock.unlock();}
static void xSemaphoreGive(Semaphore *s){
    {std::lock_guard<std::mutex> guard(s->state);s->ready=true;}
    s->changed.notify_one();
}
static void xSemaphoreTake(Semaphore *s,int){
    std::unique_lock<std::mutex> guard(s->state);
    assert(s->changed.wait_for(guard,2s,[&]{return s->ready;}));
}
static void vSemaphoreDelete(Semaphore *s){delete s;}
static void xTaskNotifyGive(Task *t){t->notified=true;changed.notify_all();}
static bool ulTaskNotifyTake(int,int){return currentTask->notified.exchange(false);}
static void vTaskDelay(int){std::this_thread::sleep_for(1ms);}
static void vTaskDelete(void*){}
static uint32_t micros(){
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
static void digitalWrite(int,int){}
static struct {template<typename... T>void printf(const char*,T...){}} Serial;
class Audio {
public:
    void loop(){
        inDecoder=true;
        std::unique_lock<std::mutex> guard(state);
        entered=true;changed.notify_all();
        assert(changed.wait_for(guard,2s,[]{return allowFinish;}));
        inDecoder=false;
    }
    void stopSong(){assert(!inDecoder);}
    ~Audio(){assert(!inDecoder);deleted=true;}
};
namespace media {void end(){assert(deleted&&!inDecoder);unmounted=true;}}
namespace sound {bool begin(){assert(unmounted);restored=true;return true;}}
struct MusicPump;
class GameMusic {
public:
    Audio *player=nullptr;
    MusicPump *pump=nullptr;
    static void pumpAudio(void*);
    void leave();
};
#include "../build/music_task_functions.h"

int main(){
    GameMusic music;Task task;
    music.player=new Audio;music.pump=new MusicPump;
    music.pump->lock=new Semaphore;music.pump->stopped=new Semaphore;music.pump->task=&task;
    std::thread worker([&]{currentTask=&task;GameMusic::pumpAudio(&music);});
    {
        std::unique_lock<std::mutex> guard(state);
        assert(changed.wait_for(guard,2s,[]{return entered;}));
    }
    std::thread leaving([&]{music.leave();});
    {
        std::unique_lock<std::mutex> guard(state);
        assert(changed.wait_for(guard,2s,[&]{return task.notified.load();}));
        // Exiting must wait: deleting a decoder/card still used by core 0 is unsafe.
        assert(inDecoder&&!deleted&&!unmounted&&!restored);
        allowFinish=true;
    }
    changed.notify_all();leaving.join();worker.join();
    assert(deleted&&unmounted&&restored&&!music.player&&!music.pump);
    std::puts("PASS: music exit joins the real audio worker before decoder deletion, SD unmount and launcher audio restoration");
}
