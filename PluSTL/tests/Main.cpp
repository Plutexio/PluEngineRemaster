//
// Created by Plutex on 2026-08-06.
//
// Entry point for the PluSTL container tests. Every test file self-registers through
// PLU_TEST, so all this has to do is run the registry and turn the failure count into an
// exit code — a CI job (or a human) only has to look at "$?".

#include "TestFramework.h"

#include <cstdio>

int main()
{
    std::printf("PluSTL tests\n");
    std::printf("============\n\n");

    const int failedTests = PluTest::RunAll();
    return failedTests == 0 ? 0 : 1;
}
