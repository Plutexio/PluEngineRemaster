#include "PluEngine/Render/GPUProfiler.h"

#include <glad/glad.h>
#include "PluEngine/Profiler.h"

namespace Plu {

    namespace {

        // GPU jest 1-3 klatek za CPU (async), więc każda nazwana sonda potrzebuje kilku
        // rotujących par query'ków - inaczej Begin tej klatki nadpisałby query, którego wynik
        // z poprzedniej klatki jeszcze nie jest gotowy. Analogicznie do TripleBuffer, tylko
        // płycej: to tylko telemetria, zgubiona próbka nie boli.
        constexpr int kSlotsPerName = 4;

        struct QuerySlot {
            UInt32 startId = 0;
            UInt32 endId = 0;
            bool pending = false;
        };

        struct QueryPool {
            QuerySlot Slots[kSlotsPerName];
            int NextSlot = 0;
        };

        HashMap<String, QueryPool>& GetPools()
        {
            static HashMap<String, QueryPool> pools;
            return pools;
        }

    }

    GPUProfileScope::GPUProfileScope(const String& name)
        : mName(name)
    {
        QueryPool& pool = GetPools()[name];
        QuerySlot& slot = pool.Slots[pool.NextSlot];
        pool.NextSlot = (pool.NextSlot + 1) % kSlotsPerName;

        if (slot.pending) {
            // GPU jeszcze nie oddało wyniku sprzed kSlotsPerName klatek dla tego slotu -
            // pomiń próbkę zamiast nadpisywać żywy query (silnie zaległy GPU to i tak news).
            mActive = false;
            return;
        }

        if (slot.startId == 0) {
            glGenQueries(1, &slot.startId);
            glGenQueries(1, &slot.endId);
        }

        glQueryCounter(slot.startId, GL_TIMESTAMP);
        mEndQuery = slot.endId;
        slot.pending = true;
        mActive = true;
    }

    GPUProfileScope::~GPUProfileScope()
    {
        if (!mActive) return;
        glQueryCounter(mEndQuery, GL_TIMESTAMP);
    }

    void GPUProfileScope::PollResults()
    {
        for (auto& pair : GetPools()) {
            const String& name = pair.first;
            QueryPool& pool = pair.second;
            for (QuerySlot& slot : pool.Slots) {
                if (!slot.pending) continue;

                GLint available = 0;
                glGetQueryObjectiv(slot.endId, GL_QUERY_RESULT_AVAILABLE, &available);
                if (!available) continue;

                GLuint64 startTime = 0, endTime = 0;
                glGetQueryObjectui64v(slot.startId, GL_QUERY_RESULT, &startTime);
                glGetQueryObjectui64v(slot.endId, GL_QUERY_RESULT, &endTime);
                slot.pending = false;

                const float durationMs = static_cast<float>(endTime - startTime) / 1000000.0f;
                // Pollowane na render-threadzie, ale mierzą pracę GPU — własny pseudo-wątek
                // "GPU", żeby filtr wątku w panelu oddzielał je od timerów CPU.
                Profiler::GetInstance()->RecordForThread(name, "GPU", durationMs);
            }
        }
    }

}
