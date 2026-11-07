#ifndef PLUENGINE_GPUPROFILER_H
#define PLUENGINE_GPUPROFILER_H

#include "PluEngine/Core.h"
#include "PluEngine/Timer.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu {

    // GPU odpowiednik PLU_PROFILE_SCOPE. CPU-side timery (Timer.h) mierzą tylko czas submitu
    // komend GL, nie faktyczne wykonanie na GPU - kolejka komend jest asynchroniczna, więc
    // realny koszt passa ujawnia się dopiero tam, gdzie CPU musi poczekać na GPU (np. SwapBuffer).
    // Ten scope mierzy czas GPU wprost przez parę znaczników GL_TIMESTAMP. Wynik jest gotowy
    // z opóźnieniem 1-3 klatek (GPU pracuje w tle za CPU) - próbki trafiają do Profilera pod
    // nazwą "GPU: <name>" dopiero gdy PollResults() je odbierze.
    //
    // Tylko wątek renderu (wymaga aktywnego kontekstu GL). PollResults() woła się raz na klatkę
    // renderu (RenderingManager::RenderThreadLoop) - odbiera gotowe wyniki i zwalnia query'ki
    // do ponownego użycia.
    class PLURENDER_API GPUProfileScope {
    public:
        explicit GPUProfileScope(const String& name);
        ~GPUProfileScope();

        static void PollResults();

    private:
        String mName;
        UInt32 mEndQuery = 0;
        bool mActive = false;
    };

}

#define PLU_PROFILE_SCOPE_GPU(name) ::Plu::GPUProfileScope PLU_TIMER_CONCAT(plu_gpu_timer_, __LINE__)(name)

#endif //PLUENGINE_GPUPROFILER_H
