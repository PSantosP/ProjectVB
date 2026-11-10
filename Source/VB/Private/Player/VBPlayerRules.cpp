// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Player/VBPlayerRules.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Map/VBPlayerRelocationComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "VB/VB.h"

namespace
{
	UAbilitySystemComponent* GetInstigatorASC(const FVBRuleContext& Context)
	{
		return Context.Instigator ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Context.Instigator) : nullptr;
	}
}

bool FVBCondition_Reputation::Evaluate(const FVBRuleContext& Context) const
{
	const UAbilitySystemComponent* ASC = GetInstigatorASC(Context);
	if (!ASC || !ASC->HasAttributeSetForAttribute(UVBReputationAttributeSet::GetReputationAttribute()))
	{
		VB_LOG(Warning, "조건(평판): 부른 플레이어의 평판을 읽을 수 없다 - 거짓으로 친다");
		return false;
	}

	return VBRules::Compare(Compare, ASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute()), Value);
}

void FVBEffect_ApplyGameplayEffect::Apply(const FVBRuleContext& Context) const
{
	UAbilitySystemComponent* ASC = GetInstigatorASC(Context);
	if (!ASC || !Effect)
	{
		VB_LOG(Warning, "효과(GE 걸기): %s - 적용 못 함", !Effect ? TEXT("GE 가 비었다") : TEXT("부른 플레이어의 ASC 가 없다"));
		return;
	}
	// 적 처치 평판(AVBEnemyBase), 의료 평판(UVBGA_MedicalAbilityBase)과 같은 모양 - 스펙 + SetByCaller + 자기에게.
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Effect, 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return;
	}

	for (const TPair<FGameplayTag, float>& Pair : SetByCallerMagnitudes)
	{
		Spec.Data->SetSetByCallerMagnitude(Pair.Key, Pair.Value);
	}

	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void FVBEffect_SaveCheckpoint::Apply(const FVBRuleContext& Context) const
{
	APawn* Pawn = Context.Instigator ? Context.Instigator->GetPawn() : nullptr;
	UVBSaveGameSubsystem* Save = Context.World ? UVBSaveGameSubsystem::Get(Context.World) : nullptr;
	if (!Pawn || !Save)
	{
		VB_LOG(Warning, "효과(체크포인트 저장): %s - 저장 못 함", !Pawn ? TEXT("부른 플레이어의 폰이 없다") : TEXT("저장 서브시스템이 없다"));
		Context.ReportEffectFailure();
		return;
	}

	const AActor* Marker = RestoreAt.Get();
	FTransform Target = Pawn->GetActorTransform();
	if (Marker)
	{
		// 캐릭터는 서 있어야 한다 - 마커의 피치, 롤은 버리고 야우만 쓴다. 높이는 복원이 지면에 맞춘다(UVBPlayerRestoreComponent).
		Target = FTransform(FRotator(0.0f, Marker->GetActorRotation().Yaw, 0.0f), Marker->GetActorLocation());
	}
	else
	{
		VB_LOG(Warning, "효과(체크포인트 저장): 돌아올 자리 표지가 %s - 지금 자리로 저장한다", RestoreAt.IsNull() ? TEXT("비었다") : TEXT("로드되지 않았다"));
	}

	if (!Save->SaveCheckpoint(Pawn, Target))
	{
		VB_LOG(Warning, "효과(체크포인트 저장): 저장 요청이 거절됐다");
		Context.ReportEffectFailure();
	}
}

void FVBEffect_MoveToPoint::Apply(const FVBRuleContext& Context) const
{
	const AActor* Marker = Destination.Get();
	APlayerController* PC = Context.Instigator ? Context.Instigator->GetPlayerController() : nullptr;
	UVBPlayerRelocationComponent* Mover = PC ? PC->FindComponentByClass<UVBPlayerRelocationComponent>() : nullptr;
	// 대신 갈 자리를 찾지 않는다 - 마커가 없으면 옮기지 않고, 폰은 그 구덩이, 복도에서 걸어 나온다.
	const TCHAR* Problem = !Marker ? (Destination.IsNull() ? TEXT("도착 표지가 비었다") : TEXT("도착 표지가 로드되지 않았다"))
		: Marker->GetWorld() != Context.World ? TEXT("도착 표지가 다른 월드에 있다")
		: !Mover ? TEXT("부른 플레이어의 컨트롤러/옮기기 컴포넌트가 없다")
		: nullptr;
	if (Problem)
	{
		VB_LOG(Warning, "효과(자리로 옮기기): %s - 옮기지 않음", Problem);
		return;
	}
	// 캐릭터는 서 있어야 한다 - 마커의 피치, 롤은 버리고 야우만 쓴다(체크포인트 저장과 같다).
	const FTransform Arrival(FRotator(0.0f, Marker->GetActorRotation().Yaw, 0.0f), Marker->GetActorLocation());
	Mover->StartScriptedMove(Arrival, FadeOutSeconds, FadeInSeconds, OnArrivedEffects);
}
