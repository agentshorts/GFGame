// Player movement defaults. Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Character;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFVoxelTypes.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFCharacterTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFCharacterStepHeightTest, "GFGame.Character.StepHeightOneBlock", GFCharacterTestFlags)
bool FGFCharacterStepHeightTest::RunTest(const FString&)
{
	const AGFCharacter* CDO = GetDefault<AGFCharacter>();
	const UCharacterMovementComponent* Move = CDO ? CDO->GetCharacterMovement() : nullptr;
	const UCapsuleComponent* Capsule = CDO ? CDO->GetCapsuleComponent() : nullptr;
	if (!TestNotNull(TEXT("Player CDO movement"), Move) || !TestNotNull(TEXT("Player CDO capsule"), Capsule))
	{
		return false;
	}

	// Players walk up one-block (100 cm) voxel steps without jumping.
	TestTrue(*FString::Printf(TEXT("MaxStepHeight %.1f cm >= one block (100 cm)"), Move->MaxStepHeight), Move->MaxStepHeight >= 100.f);
	TestTrue(TEXT("GFBlockSize is still 100 cm"), FMath::IsNearlyEqual(GFBlockSize, 100.f));
	TestEqual(TEXT("MaxStepHeight comes from PlayerMaxStepHeight"), Move->MaxStepHeight, CDO->PlayerMaxStepHeight);

	// StepUp rejects hits above the capsule cylinder top (HalfHeight - Radius above centre), so the step must fit under it.
	const float Radius = Capsule->GetUnscaledCapsuleRadius();
	const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const float CapsuleStepLimit = 2.f * HalfHeight - Radius;
	TestTrue(*FString::Printf(TEXT("Capsule allows the step (%.1f <= %.1f)"), Move->MaxStepHeight, CapsuleStepLimit), Move->MaxStepHeight <= CapsuleStepLimit);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS