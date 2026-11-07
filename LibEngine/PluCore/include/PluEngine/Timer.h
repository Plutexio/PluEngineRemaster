//
// Created by Plutex on 2026-01-24.
//

#ifndef PLUENGINE_TIMER_H
#define PLUENGINE_TIMER_H

#include <chrono>
#include <string>
#include <unordered_map>
#include "PluBase/Log.h"
#include "Profiler.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu {

    class Timer {
    public:
        // logToConsole == false (domyślnie): pomiar trafia tylko do rejestru profilera.
        // logToConsole == true: dodatkowo loguje do konsoli (progi TRACE/INFO/WARN).
        Timer(const String& name, bool logToConsole = false)
            : m_Name(name), m_Stopped(false), m_LogToConsole(logToConsole) {
            m_StartTimepoint = std::chrono::high_resolution_clock::now();
        }

        ~Timer() {
            if (!m_Stopped) Stop();
        }

        void Stop() {
            auto endTimepoint = std::chrono::high_resolution_clock::now();
            long long start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
            long long end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();

            float duration = (end - start) * 0.001f; // konwersja na milisekundy

            // Zawsze zapisujemy pomiar do globalnego rejestru profilera.
            Profiler::GetInstance()->Record(m_Name, duration);

            // Log do konsoli tylko na życzenie (np. PLU_PROFILE_SCOPE_LOG).
            if (m_LogToConsole) {
                if (duration < 0.2) {
                    PLU_CORE_TRACE("Timer {0}: {1}ms", m_Name.CStr(), duration);
                } else if (duration < 0.8) {
                    PLU_CORE_INFO("Timer {0}: {1}ms", m_Name.CStr(), duration);
                } else {
                    PLU_CORE_WARN("Timer {0}: {1}ms", m_Name.CStr(), duration);
                }
            }
            m_Stopped = true;
        }

    private:
        String m_Name;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimepoint;
        bool m_Stopped;
        bool m_LogToConsole;
    };

    // Singleton do zarządzania ręcznymi timerami (dla PLU_TIMER_END)
    class TimerManager {
    public:
        static void StartTimer(const String& name, bool logToConsole = false) {
            GetTimers().Insert(name, CreateOwning<Timer>(name, logToConsole));
        }

        static void EndTimer(const String& name) {
            auto& timers = GetTimers();
            if (timers.Contains(name)) {
                timers.Remove(name); // Destruktor klasy Timer zajmie się logowaniem
            } else {
                PLU_CORE_WARN("Tried to stop non-existing timer: {0}", name.CStr());
            }
        }

    private:
        static HashMap<String, TOwningPointer<Timer>>& GetTimers() {
            static HashMap<String, TOwningPointer<Timer>> s_Timers;
            return s_Timers;
        }
    };
}

// --- Makra ---

// Podwójna indirekcja, żeby __LINE__ rozwinęło się przed sklejeniem nazwy zmiennej.
#define PLU_TIMER_CONCAT_INNER(a, b) a##b
#define PLU_TIMER_CONCAT(a, b) PLU_TIMER_CONCAT_INNER(a, b)

// Automatyczny timer działający do końca bieżącego scope'a (cicho, tylko do rejestru).
#define PLU_PROFILE_SCOPE(name)     ::Plu::Timer PLU_TIMER_CONCAT(plu_timer_, __LINE__)(name, false)
// Jak wyżej, ale dodatkowo loguje pomiar do konsoli.
#define PLU_PROFILE_SCOPE_LOG(name) ::Plu::Timer PLU_TIMER_CONCAT(plu_timer_, __LINE__)(name, true)

// Ręczne sterowanie timerami (w stylu JS). Opcjonalny drugi argument = log do konsoli.
#define PLU_TIMER_START(name, ...) ::Plu::TimerManager::StartTimer(name, ##__VA_ARGS__)
#define PLU_TIMER_END(name)        ::Plu::TimerManager::EndTimer(name)

#endif //PLUENGINE_TIMER_H