// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "CoreLoop/VBServerWrite.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

// 쓰기 검사: 서버 쪽과 소유자 없는 쪽은 통과하고, 클라 쪽은 경고와 함께 막힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBServerWriteTest, "VB.Unit.Core.ServerWrite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBServerWriteTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USceneComponent> Ownerless(NewObject<USceneComponent>(GetTransientPackage()));
	TestTrue(TEXT("no owner passes"), VBServerWrite::IsAllowed(*Ownerless, TEXT("Test"), TEXT("write")));
	TestFalse(TEXT("no owner has no owner authority"), VBServerWrite::HasOwnerAuthority(*Ownerless));

	TStrongObjectPtr<AActor> Owner(NewObject<AActor>(GetTransientPackage()));
	USceneComponent* Component = NewObject<USceneComponent>(Owner.Get());
	TestTrue(TEXT("authority passes"), VBServerWrite::IsAllowed(*Component, TEXT("Test"), TEXT("write")));
	TestTrue(TEXT("server owner has authority"), VBServerWrite::HasOwnerAuthority(*Component));

	Owner->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("client owner has no authority"), VBServerWrite::HasOwnerAuthority(*Component));
	AddExpectedMessage(TEXT("권한 없는 쪽에서 불렀다"), ELogVerbosity::Warning);
	TestFalse(TEXT("client is refused"), VBServerWrite::IsAllowed(*Component, TEXT("Test"), TEXT("write")));
	return true;
}

#endif
