// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBAreaTrigger.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "TimerManager.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBAreaTrigger"

bool VBAreaTrigger::ShouldFire(bool bWasSatisfied, bool bNowSatisfied, bool bOnce, bool bOnceAlreadyFired)
{
	if (!bNowSatisfied || bWasSatisfied)
	{
		return false;
	}

	return !(bOnce && bOnceAlreadyFired);
}

bool VBAreaTrigger::NeedsReadyPlayer(const TArray<FInstancedStruct>& Effects)
{
	return Effects.ContainsByPredicate([](const FInstancedStruct& Effect)
	{
		const FVBEffect* Typed = Effect.GetPtr<FVBEffect>();
		return Typed && Typed->NeedsReadyPlayer();
	});
}

int32 VBAreaTrigger::CountDeferredEffects(const TArray<FInstancedStruct>& Effects)
{
	return Effects.FilterByPredicate([](const FInstancedStruct& Effect)
	{
		const FVBEffect* Typed = Effect.GetPtr<FVBEffect>();
		return Typed && Typed->ReportsDeferredResult();
	}).Num();
}

AVBAreaTrigger::AVBAreaTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	SetRootComponent(Area);
	Area->SetBoxExtent(FVector(200.0f, 200.0f, 150.0f));
	// 겹침만 낸다. 막지 않는다 - 영역은 보이지 않는 판정이다.
	Area->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	Area->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Area->SetCollisionObjectType(ECC_WorldDynamic);
	Area->SetCollisionResponseToAllChannels(ECR_Ignore);
	Area->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Area->SetGenerateOverlapEvents(true);
	Area->SetHiddenInGame(true);
}

void AVBAreaTrigger::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	Area->OnComponentBeginOverlap.AddDynamic(this, &AVBAreaTrigger::HandleBeginOverlap);
	Area->OnComponentEndOverlap.AddDynamic(this, &AVBAreaTrigger::HandleEndOverlap);

	// 이미 안에서 시작한 플레이어도 들어옴으로 친다 - 겹침 신호만 기다리면 그 플레이어를 영영 못 본다.
	RefreshPlayerBindings();
	// 사실이 바뀔 때마다, 그리고 묶는 즉시 한 번 평가를 예약한다.
	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &AVBAreaTrigger::RequestEvaluate));
}

void AVBAreaTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	DisconnectPlayerBindings();
	Area->OnComponentBeginOverlap.RemoveDynamic(this, &AVBAreaTrigger::HandleBeginOverlap);
	Area->OnComponentEndOverlap.RemoveDynamic(this, &AVBAreaTrigger::HandleEndOverlap);
	StoryBinding.Disconnect();
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AVBAreaTrigger::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bEnding)
	{
		return;
	}

	RequestEvaluate();
	OnOccupancyChanged.Broadcast();
}

void AVBAreaTrigger::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (bEnding)
	{
		return;
	}

	RequestEvaluate();
	OnOccupancyChanged.Broadcast();
}

void AVBAreaTrigger::RefreshPlayerBindings()
{
	if (!bRequirePlayerPreparation || bEnding || !GetWorld())
	{
		return;
	}

	PlayerBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &ThisClass::RequestEvaluate));
}

void AVBAreaTrigger::DisconnectPlayerBindings()
{
	PlayerBinding.Disconnect();
}

void AVBAreaTrigger::RequestEvaluate()
{
	if (bEnding || bEvaluatePending || !HasAuthority() || !GetWorld())
	{
		return;
	}

	bEvaluatePending = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &AVBAreaTrigger::Evaluate);
}

void AVBAreaTrigger::Evaluate()
{
	bEvaluatePending = false;
	UWorld* World = GetWorld();
	if (bEnding || !World || !HasAuthority())
	{
		return;
	}

	RefreshPlayerBindings();
	const bool bOnce = OnceFact.IsValid();
	if (bRequirePlayerPreparation && !bOnce)
	{
		if (!bWarnedMissingOnce)
		{
			bWarnedMissingOnce = true;
			VB_LOG(Warning, "%s: 준비 검사는 OnceFact가 있는 일회 영역에만 쓴다 - 발화하지 않는다", *GetName());
		}

		return;
	}

	const UVBStoryStateComponent* Story = StoryBinding.GetStory();
	if (bOnce && !Story)
	{
		// 한 번만을 기록할 곳이 없다 - 발화하면 들어올 때마다 반복된다. 못 지키는 약속을 통과로 떨어뜨리지 않고 멈춘다.
		if (!bWarnedNoStory)
		{
			bWarnedNoStory = true;
			VB_LOG(Warning, "%s: 한 번만 영역인데 세계 사실(UVBStoryStateComponent)이 없다 - 발화하지 않는다", *GetName());
		}

		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!IsValid(Pawn) || (bRequirePlayerPreparation && PlayerController != PlayerBinding.GetController()))
		{
			continue;
		}

		const bool bNow = IsSatisfiedFor(*Pawn);
		const bool bWas = LastSatisfied.FindOrAdd(Pawn, false);
		const bool bFired = bOnce && Story->HasFact(OnceFact);
		const bool bFire = VBAreaTrigger::ShouldFire(bWas, bNow, bOnce, bFired);
		// 마지막 admission 실패는 edge도 소비하지 않는다. 효과 중 재진입에 map 원소 참조를 보관하지 않는다.
		if (!bRequirePlayerPreparation)
		{
			LastSatisfied.FindOrAdd(Pawn) = bNow;
			if (bFire)
			{
				TryFire(*Pawn);
			}

			if (bEnding)
			{
				return;
			}

			continue;
		}

		const bool bAdmitted = bFire && TryFire(*Pawn);
		if (bEnding)
		{
			return;
		}

		LastSatisfied.FindOrAdd(Pawn) = bFire ? bAdmitted : bNow;
	}

	// 사라진 폰(죽음, 다시 낳음)의 기록을 지운다. 다시 낳은 폰은 새 열쇠라 거짓에서 다시 시작한다.
	for (auto It = LastSatisfied.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (bRequirePlayerPreparation && It.Key().Get() != PlayerBinding.GetPawn()))
		{
			It.RemoveCurrent();
		}
	}
}

bool AVBAreaTrigger::IsPawnInside(const APawn* Pawn) const
{
	const UWorld* World = GetWorld();
	return !bEnding && !IsActorBeingDestroyed() && World
		&& IsValid(Pawn)
		&& IsValid(Area.Get())
		&& Pawn->GetWorld() == World
		&& Area->IsOverlappingActor(Pawn);
}

bool AVBAreaTrigger::PlaysSceneOnce(const UObject* Scene) const
{
	if (!Scene || !OnceFact.IsValid())
	{
		return false;
	}

	for (const FInstancedStruct& Effect : Effects)
	{
		const FVBEffect* Rule = Effect.GetPtr<FVBEffect>();
		if (Rule && Rule->GetPlayedScene() == Scene)
		{
			return true;
		}
	}

	return false;
}

bool AVBAreaTrigger::IsSatisfiedFor(APawn& Pawn)
{
	if (!IsPawnInside(&Pawn))
	{
		return false;
	}

	FVBRuleContext Context = FVBRuleContext::Make(this, Pawn.GetPlayerState());
	Context.Counterpart = this;
	if (!VBRules::EvaluateAll(Conditions, Context))
	{
		return false;
	}

	if (!bRequirePlayerPreparation)
	{
		return true;
	}

	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	return OnceFact.IsValid() && IsValid(Controller) && Controller->GetPawn() == &Pawn
		&& Controller->PlayerState == Pawn.GetPlayerState()
		&& UVBPlayerReadinessComponent::QueryForPawn(&Pawn).State == EVBPlayerPreparationState::Ready;
}

bool AVBAreaTrigger::TryFire(APawn& Pawn)
{
	// 사실/사건/효과 바로 앞에서 현재 점유, 조건, 준비, Once를 다시 확인한다.
	if (bEnding || bScenePending || !HasAuthority() || (bRequirePlayerPreparation && !IsSatisfiedFor(Pawn)))
	{
		return false;
	}

	FVBRuleContext Context = FVBRuleContext::Make(this, Pawn.GetPlayerState());
	Context.Counterpart = this;
	if (OnceFact.IsValid() && (!Context.Story || Context.Story->HasFact(OnceFact)))
	{
		return false;
	}

	VB_LOG(Log, "영역 진입: %s <- %s", *GetName(), *GetNameSafe(&Pawn));

	// 장면이 있는 일회 영역: 사실은 장면이 완료, 건너뛰기로 닫힐 때 쓴다 - 거절, 취소된 장면이 영역을 소진하지 않게. 그동안은 대기 상태가 재발화를 막는다.
	const bool bDeferOnce = OnceFact.IsValid() && VBAreaTrigger::CountDeferredEffects(Effects) > 0;
	if (bDeferOnce)
	{
		bScenePending = true;
		Context.OnDeferredClosed = FVBOnDeferredEffectClosed::CreateWeakLambda(this,
			[this, Player = TWeakObjectPtr<APlayerState>(Pawn.GetPlayerState())](bool bCompleted)
			{
				HandleSceneClosed(bCompleted, Player);
			});
	}
	// 그 밖의 일회 영역은 사실 먼저 - 효과가 이 사실을 읽거나, 효과가 바꾼 사실로 다시 판정할 때 이미 발화함이어야 한다.
	else if (OnceFact.IsValid())
	{
		FVBEffect_SetFact MarkFired;
		MarkFired.Fact = OnceFact;
		MarkFired.bOn = true;
		MarkFired.Apply(Context);
	}

	if (!EventTags.IsEmpty())
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			FVBGameEvent Event;
			Event.EventTag = VBGameplayTags::Event_Area_Entered;
			Event.TargetTags = EventTags;
			Event.Instigator = Pawn.GetPlayerState();
			Events->Broadcast(Event);
		}
	}

	if (!Effects.IsEmpty() && !VBRules::ApplyAll(Effects, Context) && bScenePending)
	{
		// 효과가 하나도 돌지 않았으면 장면도 요청되지 않았다.
		bScenePending = false;
	}

	return true;
}

void AVBAreaTrigger::HandleSceneClosed(bool bCompleted, TWeakObjectPtr<APlayerState> Player)
{
	bScenePending = false;
	if (!bCompleted)
	{
		VB_LOG(Log, "영역 %s: 장면이 성공으로 닫히지 않았다(거절 · 취소 · 완료 효과 실패) - %s 를 쓰지 않는다(다음 진입에 다시)", *GetName(), *OnceFact.ToString());
		return;
	}

	FVBRuleContext Context = FVBRuleContext::Make(this, Player.Get());
	Context.Counterpart = this;
	FVBEffect_SetFact MarkFired;
	MarkFired.Fact = OnceFact;
	MarkFired.bOn = true;
	MarkFired.Apply(Context);
}

#if WITH_EDITOR
EDataValidationResult AVBAreaTrigger::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (bRequirePlayerPreparation && !OnceFact.IsValid())
	{
		Context.AddError(LOCTEXT("PreparationNeedsOnce", "플레이어 준비 검사는 OnceFact가 있는 일회 영역에만 사용할 수 있습니다."));
		Result = EDataValidationResult::Invalid;
	}
	// 장면처럼 플레이어를 가져가는 효과는 준비 뒤에만 건다 - 시작 지점의 영역은 생성, 복원 중에 겹쳐 효과를 건다.
	if (VBAreaTrigger::NeedsReadyPlayer(Effects) && !(bRequirePlayerPreparation && OnceFact.IsValid()))
	{
		Context.AddError(LOCTEXT("ReadyPlayerEffect", "준비된 플레이어가 필요한 효과(시네마틱 등)가 있다 - bRequirePlayerPreparation 과 OnceFact 를 켠다."));
		Result = EDataValidationResult::Invalid;
	}
	// 일회 영역의 사실은 장면 하나의 결과를 따른다 - 장면이 둘이면 어느 결과가 사실을 정할지 정해지지 않는다.
	if (OnceFact.IsValid() && VBAreaTrigger::CountDeferredEffects(Effects) > 1)
	{
		Context.AddError(LOCTEXT("OneDeferredEffect", "일회 영역에는 장면(나중에 끝나는 효과)을 하나만 둘 수 있다."));
		Result = EDataValidationResult::Invalid;
	}

	if (!VBRules::ValidateStructArray(Conditions, FVBCondition::StaticStruct(), LOCTEXT("ConditionsWhere", "조건"), Context)
		|| !VBRules::ValidateStructArray(Effects, FVBEffect::StaticStruct(), LOCTEXT("EffectsWhere", "효과"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	if (Effects.IsEmpty() && EventTags.IsEmpty() && !OnceFact.IsValid())
	{
		Context.AddWarning(LOCTEXT("DoesNothing", "효과·사건 태그·한 번만 사실이 모두 비었다 - 들어와도 아무 일이 없다."));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
