//
// Created by Plutex.
//

#include "PluEngine/Core/Objects/EngineObject.h"

namespace Plu
{
    // Lifetime is confined per-object to its creating (owning) thread, not to the main
    // thread: render-owned resources are legitimately created and destroyed on the render
    // thread. The owning-thread guarantee is enforced in the
    // pointer layer (ControlBlock::owningThread asserts in TOwningPointer::Release), so no
    // thread check is needed here. Out-of-line anchor for the virtual destructor.
    EngineObject::~EngineObject() = default;
}
