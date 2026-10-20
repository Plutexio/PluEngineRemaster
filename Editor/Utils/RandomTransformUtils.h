//
// Created by Plutex on 2026-07-19.
//

#ifndef PLUENGINE_RANDOMTRANSFORMUTILS_H
#define PLUENGINE_RANDOMTRANSFORMUTILS_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluSTL/Array/Array.h"

namespace Plu
{
    /**
     * Generatory losowych transformów do rozrzucania obiektów w edytorze.
     *
     * Każda funkcja **czyści** tablicę wyjściową i wypełnia ją `count` wartościami
     * losowanymi per-oś z przedziału `[min, max]` (włącznie). `nullptr` jest ignorowany,
     * odwrócone limity (`min > max`) są normalizowane.
     *
     * `seed == RandomTransformSeedAuto` (domyślnie) → losowanie nie jest powtarzalne.
     * Dowolny inny seed daje ten sam wynik przy tych samych argumentach.
     */
    constexpr UInt64 RandomTransformSeedAuto = 0;

    /** Losowe lokacje w prostopadłościanie `[min, max]`. */
    void GenerateRandomLocations(DynamicArray<Vec3>* outLocations, UInt32 count,
                                 const Vec3& min, const Vec3& max,
                                 UInt64 seed = RandomTransformSeedAuto);

    /** Losowe rotacje w **stopniach** (pitch=X, yaw=Y, roll=Z), per-oś z `[min, max]`. */
    void GenerateRandomRotations(DynamicArray<Vec3>* outRotations, UInt32 count,
                                 const Vec3& min, const Vec3& max,
                                 UInt64 seed = RandomTransformSeedAuto);

    /** Losowe skale, per-oś z `[min, max]` (niejednorodne). */
    void GenerateRandomScales(DynamicArray<Vec3>* outScales, UInt32 count,
                              const Vec3& min, const Vec3& max,
                              UInt64 seed = RandomTransformSeedAuto);

    /** Losowe skale jednorodne — jeden mnożnik z `[min, max]` na wszystkie osie. */
    void GenerateRandomUniformScales(DynamicArray<Vec3>* outScales, UInt32 count,
                                     float min, float max,
                                     UInt64 seed = RandomTransformSeedAuto);
}

#endif //PLUENGINE_RANDOMTRANSFORMUTILS_H
