//
// Created by Plutex on 2026-06-20.
//

#ifndef PLUENGINE_PROFILER_H
#define PLUENGINE_PROFILER_H

#include "Core.h"
#include "PluSTL_FWD.h"
#include "Concurrent/ConcurrentHashMap.h"

namespace Plu {

    // Pojedynczy wpis profilera: nazwa -> historia ostatnich pomiarów + statystyki.
    // History to ring buffer; WriteIndex wskazuje następne miejsce do zapisu.
    // Wpisy są rozdzielone per wątek: ten sam timer zmierzony na Main i na Render
    // daje dwa osobne wpisy (patrz Profiler::MakeKey).
    struct ProfilerEntry {
        static constexpr Int32 kHistorySize = 120;
        String Name;                      // nazwa timera (bez prefiksu wątku)
        String ThreadName;                // wątek, z którego przyszły pomiary
        float History[kHistorySize] = {}; // ostatnie próbki w ms
        Int32 WriteIndex = 0;              // następna pozycja zapisu (offset dla PlotLines)
        Int32 SampleCount = 0;             // ile realnych próbek (<= kHistorySize)
        float LastMs = 0.0f;
        float MinMs = 0.0f;
        float MaxMs = 0.0f;
        float AvgMs = 0.0f;               // średnia z aktualnego okna historii
        UInt64 TotalCalls = 0;            // całkowita liczba zarejestrowanych pomiarów
    };

    // Globalny rejestr timingów. Plain singleton (nie EngineObject).
    // Thread-safe: pomiary trafiają z wątku gry / render-threada, panel czyta z wątku UI.
    class PLUCORE_API Profiler {
    public:
        static Profiler* GetInstance();

        // Dopisuje pomiar do historii danego timera (tworzy wpis, jeśli nie istnieje).
        // Wpis jest przypisany do wątku wołającego (GetCurrentThreadName).
        void Record(const String& name, float durationMs);

        // Jak wyżej, ale z jawnie podanym wątkiem — dla pomiarów zbieranych na innym
        // wątku niż ten, który je odczytuje (np. GPU timery pollowane na Render).
        void RecordForThread(const String& name, const String& threadName, float durationMs);

        // Kopia rejestru do bezpiecznego odczytu przez panel. Klucz = MakeKey(...).
        HashMap<String, ProfilerEntry> Snapshot();

        // Nazwy wątków, z których są jakiekolwiek pomiary (posortowane, do filtra w UI).
        DynamicArray<String> SnapshotThreadNames();

        // Czyści wszystkie zebrane timingi.
        void Clear();

        // The whole registry as CSV text: one row per entry, summary columns followed by the
        // history ring buffer unwrapped into chronological Sample0..SampleN columns (rows with
        // fewer samples are padded with empty cells, so every row has the same width).
        // threadFilter, when non-empty, keeps only entries recorded on that thread.
        // Returns text only — writing it out is the caller's job (DiskManager::SaveText).
        String BuildCsv(const String& threadFilter = String());

        // Klucz wpisu: "wątek|nazwa". Ten sam timer na dwóch wątkach = dwa wpisy.
        static String MakeKey(const String& name, const String& threadName);

    private:
        Profiler() = default;

        // Striped by key, and the key is "thread|name" — so Main's timers and Render's
        // timers land on different stripes and the two threads mostly stop contending at
        // all. That matters here: CLAUDE.md actively encourages sprinkling
        // PLU_PROFILE_SCOPE everywhere, so Record() is on every profiled path of both
        // threads. The single mutex this replaced also serialized Record() against the
        // panel's per-frame full-map Snapshot().
        ConcurrentHashMap<String, ProfilerEntry> mEntries;
    };

}

#endif //PLUENGINE_PROFILER_H
