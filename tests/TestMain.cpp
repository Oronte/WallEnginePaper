// Point d'entree des tests ctest (cible WallEnginePaperTests).
#include <cstdio>

#include "TestUtil.h"

void RunSpectrumAnalyzerTests();
void RunBeatDetectorTests();

int main() {
    RunSpectrumAnalyzerTests();
    RunBeatDetectorTests();
    std::printf("\n%s (%d echec(s))\n", lwe::test::g_failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT",
                lwe::test::g_failures);
    return lwe::test::g_failures == 0 ? 0 : 1;
}
