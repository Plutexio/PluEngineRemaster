//
// Created by Plutex on 2026-06-20.
//

#include "PluEngine/Profiler.h"

#include <algorithm>
#include <cstdio>

#include "PluEngine/Core/Threading/ThreadAffinity.h"

namespace Plu {

    Profiler* Profiler::GetInstance()
    {
        // Function-local static: gwarantowana pojedyncza, thread-safe inicjalizacja.
        static Profiler instance;
        return &instance;
    }

    String Profiler::MakeKey(const String& name, const String& threadName)
    {
        return threadName + "|" + name;
    }

    void Profiler::Record(const String& name, float durationMs)
    {
        RecordForThread(name, GetCurrentThreadName(), durationMs);
    }

    void Profiler::RecordForThread(const String& name, const String& threadName, float durationMs)
    {
        // Runs under the entry's stripe lock — ring-buffer arithmetic only, no allocation,
        // no I/O, no re-entry into mEntries. See the ConcurrentHashMap header for the rule.
        auto applySample = [durationMs](ProfilerEntry& entry)
        {
            entry.History[entry.WriteIndex] = durationMs;
            entry.WriteIndex = (entry.WriteIndex + 1) % ProfilerEntry::kHistorySize;
            if (entry.SampleCount < ProfilerEntry::kHistorySize) {
                entry.SampleCount++;
            }
            entry.LastMs = durationMs;
            entry.TotalCalls++;

            // Statystyki z aktualnego okna historii.
            float sum = 0.0f;
            float minMs = entry.History[0];
            float maxMs = entry.History[0];
            for (Int32 i = 0; i < entry.SampleCount; i++) {
                const float v = entry.History[i];
                sum += v;
                if (v < minMs) minMs = v;
                if (v > maxMs) maxMs = v;
            }
            entry.MinMs = minMs;
            entry.MaxMs = maxMs;
            entry.AvgMs = entry.SampleCount > 0 ? sum / static_cast<float>(entry.SampleCount) : 0.0f;
        };

        const String key = MakeKey(name, threadName);

        // Steady state: the entry exists, so one Visit is the whole call — no prototype
        // ProfilerEntry is built (it carries a 120-float history, zeroing it on every sample
        // would be pure waste on a path both threads hit constantly).
        if (mEntries.Visit(key, applySample)) return;

        // First sample for this timer on this thread — build the entry and seed it.
        ProfilerEntry fresh;
        fresh.Name = name;
        fresh.ThreadName = threadName;
        mEntries.VisitOrInsert(key, applySample, fresh);
    }

    HashMap<String, ProfilerEntry> Profiler::Snapshot()
    {
        return mEntries.Snapshot(); // głęboka kopia, stripe po stripie
    }

    DynamicArray<String> Profiler::SnapshotThreadNames()
    {
        DynamicArray<String> names;
        // The callback runs under a stripe lock, but the engine only ever has a handful of
        // named threads, so after the first couple of entries this is a pure scan.
        mEntries.ForEach([&names](const String&, const ProfilerEntry& entry) {
            const String& threadName = entry.ThreadName;
            for (const String& existing : names) {
                if (existing == threadName) return;
            }
            names.PushBack(threadName);
        });

        // Stabilna kolejność w combo — inaczej pozycje skakałyby przy rehashu mapy.
        std::sort(names.begin(), names.end(), [](const String& a, const String& b) { return a.Compare(b) < 0; });
        return names;
    }

    void Profiler::Clear()
    {
        mEntries.Clear();
    }

    namespace
    {
        // RFC 4180 quoting: only needed when the value carries a separator, a quote or a newline.
        // Timer names are author-supplied strings, so this is not hypothetical.
        String CsvEscape(const String& value)
        {
            bool needsQuotes = false;
            for (const char* c = value.CStr(); *c; ++c) {
                if (*c == ',' || *c == '"' || *c == '\n' || *c == '\r') {
                    needsQuotes = true;
                    break;
                }
            }
            if (!needsQuotes) return value;

            String result = "\"";
            for (const char* c = value.CStr(); *c; ++c) {
                if (*c == '"') result += '"'; // doubled to escape
                result += *c;
            }
            result += '"';
            return result;
        }

        String FormatMs(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);
            return buffer;
        }
    }

    String Profiler::BuildCsv(const String& threadFilter)
    {
        HashMap<String, ProfilerEntry> snapshot = Snapshot(); // frozen copy, no lock held below

        String csv = "Name,Thread,LastMs,AvgMs,MinMs,MaxMs,TotalCalls,SampleCount";
        for (Int32 i = 0; i < ProfilerEntry::kHistorySize; i++) {
            csv += ",Sample";
            csv += String::FromInt(i);
        }
        csv += "\n";

        for (const auto& pair : snapshot) {
            const ProfilerEntry& entry = pair.second;
            if (!threadFilter.IsEmpty() && entry.ThreadName != threadFilter) continue;

            csv += CsvEscape(entry.Name);
            csv += ",";
            csv += CsvEscape(entry.ThreadName);
            csv += ",";
            csv += FormatMs(entry.LastMs);
            csv += ",";
            csv += FormatMs(entry.AvgMs);
            csv += ",";
            csv += FormatMs(entry.MinMs);
            csv += ",";
            csv += FormatMs(entry.MaxMs);
            csv += ",";
            csv += String::FromInt(static_cast<Int64>(entry.TotalCalls));
            csv += ",";
            csv += String::FromInt(entry.SampleCount);

            // Unwrap the ring buffer oldest-to-newest. Once it has wrapped, the oldest sample sits
            // at WriteIndex; before that the buffer is still filling from index 0 upwards.
            const bool wrapped = entry.SampleCount == ProfilerEntry::kHistorySize;
            for (Int32 i = 0; i < ProfilerEntry::kHistorySize; i++) {
                csv += ",";
                if (i >= entry.SampleCount) continue; // pad short rows to a constant width
                const Int32 index = wrapped ? (entry.WriteIndex + i) % ProfilerEntry::kHistorySize : i;
                csv += FormatMs(entry.History[index]);
            }
            csv += "\n";
        }
        return csv;
    }

}
