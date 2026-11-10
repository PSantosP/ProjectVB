// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AI/VBAIController.h"

#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBEncounterGateComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BrainComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"
#include "VB/VB.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBDeathFacts.h"


AVBAIController::AVBAIController()
{
	// AIPerception 컴포넌트를 직접 생성해 설정한다.
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception")));

	// Sight Sense 설정
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));

	// 다른 팀(Hostile)만 감지한다. 팀 인터페이스가 없는 소품은 Neutral 이라 걸러진다.
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	// 피격 감각은 소속 필터가 없어 AcceptsStimulusFrom 이 Hostile 만 거른다.
	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));

	// 시야 수치는 UVBEnemyConfig 가 갖는다. 여기선 CDO 로 시딩만 하고 적별 값은 OnPossess 에서 적용한다.
	ApplyPerceptionConfig(GetDefault<UVBEnemyConfig>());

	GetPerceptionComponent()->SetDominantSense(UAISenseConfig_Sight::StaticClass());
}

void AVBAIController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	const APawn* ControlledPawn = GetPawn();
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ControlledPawn);
	if (ControlledPawn && UVBAbilitySystemStatics::IsFinisherWindowArmed(ASC) &&
		!UVBAbilitySystemStatics::IsAttacking(ASC))
	{
		SetControlRotation(ControlledPawn->GetActorRotation());
		return;
	}

	Super::UpdateControlRotation(DeltaTime, bUpdatePawn);
}

void AVBAIController::ApplyPerceptionConfig(const UVBEnemyConfig* Config)
{
	if (!Config || !SightConfig || !DamageConfig || !GetPerceptionComponent())
	{
		return;
	}

	SightConfig->SightRadius = Config->SightRadius;
	SightConfig->LoseSightRadius = Config->LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = Config->PeripheralVisionAngleDegrees; // 반각 - 60이면 정면 120도
	SightConfig->SetMaxAge(Config->SightMaxAge);

	// 지각 시스템은 등록 때 만든 사본만 읽으므로 값을 쓴 뒤 ConfigureSense 로 사본을 다시 만든다.
	GetPerceptionComponent()->ConfigureSense(*SightConfig);

	// 피격도 목격과 같은 시간 동안 기억한다(0 = 잊지 않음).
	DamageConfig->SetMaxAge(Config->SightMaxAge);
	GetPerceptionComponent()->ConfigureSense(*DamageConfig);

	// 리스너의 감각 목록은 등록 때 복사되므로 나중에 더한 피격 감각을 반영하려면 갱신해야 한다.
	//   등록 뒤에만 - 생성자에서 OnRegister 보다 먼저 리스너를 만들면 안 된다.
	if (GetPerceptionComponent()->IsRegistered())
	{
		GetPerceptionComponent()->RequestStimuliListenerUpdate();
	}
}

void AVBAIController::BeginPlay()
{
	Super::BeginPlay();

	// 델리게이트 바인딩은 BeginPlay에서 해야한다.
	if (GetPerceptionComponent())
	{
		GetPerceptionComponent()->OnTargetPerceptionInfoUpdated.
			AddDynamic(this, &AVBAIController::OnTargetPerceptionInfoUpdated);
	}
	else
	{
		VB_LOG(Warning, "GetPerceptionComponent Is Null");
	}

	if (UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(this))
	{
		DeathListener = Bus->RegisterListener(VBGameplayTags::Message_Death, this, &ThisClass::HandleActorDied);
	}
	else
	{
		VB_LOG(Warning, "%s: 메시지 버스가 없어 죽음 알림을 못 듣는다 - 죽은 대상을 계속 노린다", *GetName());
	}
	// 듣기 전에 이미 죽은 대상을 잡고 있었으면 그것만 비운다(버스는 지난 알림을 다시 주지 않는다).
	if (const UBlackboardComponent* BB = GetBlackboardComponent())
	{
		const AActor* Target = Cast<AActor>(BB->GetValueAsObject(TargetActorKeyName));
		if (Target && UVBAbilitySystemStatics::IsActorDead(Target))
		{
			ClearTargetIfItIs(Target);
		}
	}
}

void AVBAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 적별 시야 수치를 적용한다. 스폰 적은 BeginPlay 뒤에 빙의하므로 폰을 아는 첫 시점이 여기다.
	//   Super 뒤 - 지각 리스너의 body actor 가 폰이다.
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(InPawn);
	ApplyPerceptionConfig(Enemy ? Enemy->GetEnemyConfig() : GetDefault<UVBEnemyConfig>());

	// 지각은 듣는 쪽의 팀을 컨트롤러에서 읽으므로 컨트롤러 팀을 폰에 맞춘다.
	if (const IGenericTeamAgentInterface* TeamPawn = Cast<const IGenericTeamAgentInterface>(InPawn))
	{
		const FGenericTeamId PawnTeam = TeamPawn->GetGenericTeamId();
		SetGenericTeamId(PawnTeam);

		// 팀이 없으면 아무것도 못 보는 조용한 실패라 경고를 남긴다.
		if (PawnTeam == FGenericTeamId::NoTeam)
		{
			VB_LOG(Warning, "%s: 폰 %s 의 팀이 None - 이 AI 는 아무도 감지하지 못한다. Config/BP 의 Team 확인",
				*GetName(), *InPawn->GetName());
		}
	}

	// 조우 문이 닫혀 있으면 행동 트리를 미루고 OnWoken 에 시작한다. 앞 빙의의 구독은 먼저 푼다.
	UnbindEncounterGate();
	UVBEncounterGateComponent* Gate = InPawn->FindComponentByClass<UVBEncounterGateComponent>();
	if (Gate)
	{
		// 보류(시네마틱 동안)는 깨어남과 따로 오므로 항상 듣는다.
		BoundEncounterGate = Gate;
		EncounterHeldHandle = Gate->OnHeldChanged.AddUObject(this, &AVBAIController::HandleEncounterHeldChanged);
	}

	if (Gate && !Gate->IsAwake())
	{
		EncounterWokenHandle = Gate->OnWoken.AddUObject(this, &AVBAIController::StartBehavior);
		VB_LOG(Log, "AI BT 대기: %s - 조우 문이 닫혀 있다", *InPawn->GetName());
		return;
	}

	StartBehavior();
}

void AVBAIController::OnUnPossess()
{
	UnbindEncounterGate();
	// 다른 폰을 빙의하면 옛 폰 때 걸린 잊기가 새 칠판의 대상을 지우지 않게 한다.
	CancelLostTargetMemory();
	Super::OnUnPossess();
}

void AVBAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeathListener.Unregister();
	UnbindEncounterGate();
	CancelLostTargetMemory();
	Super::EndPlay(EndPlayReason);
}

void AVBAIController::CancelLostTargetMemory()
{
	GetWorldTimerManager().ClearTimer(LostTargetTimerHandle);
	LostTarget.Reset();
}

void AVBAIController::UnbindEncounterGate()
{
	if (UVBEncounterGateComponent* Gate = BoundEncounterGate.Get())
	{
		Gate->OnHeldChanged.Remove(EncounterHeldHandle);
		Gate->OnWoken.Remove(EncounterWokenHandle);
	}

	BoundEncounterGate.Reset();
	EncounterHeldHandle.Reset();
	EncounterWokenHandle.Reset();
}

void AVBAIController::HandleEncounterHeldChanged(bool bHeld)
{
	UBrainComponent* Brain = GetBrainComponent();
	if (!Brain)
	{
		// 아직 행동 트리 전(깨기 전)이다. 깨어날 때 StartBehavior 가 보류를 다시 본다.
		return;
	}
	// ResumeLogic 은 멈춘 동안 쌓인 칠판 통지를 그대로 내보낸다.
	if (bHeld)
	{
		Brain->PauseLogic(TEXT("EncounterHold"));
	}
	else
	{
		Brain->ResumeLogic(TEXT("EncounterHold"));
	}
}

void AVBAIController::StartBehavior()
{
	if (!DefaultBehaviorTree || !GetPawn())
	{
		return;
	}

	RunBehaviorTree(DefaultBehaviorTree);
	// 보류 중에 깨어났으면 멈춘 채 시작한다 - 장면이 끝나 보류가 풀릴 때 잇는다.
	if (const UVBEncounterGateComponent* Gate = GetPawn()->FindComponentByClass<UVBEncounterGateComponent>(); Gate && Gate->IsHeld())
	{
		HandleEncounterHeldChanged(true);
	}

	VB_LOG(Log, "AI BT 시작: %s → %s",
		*GetPawn()->GetName(), *DefaultBehaviorTree.GetName());
}

void AVBAIController::OnTargetPerceptionInfoUpdated(const FActorPerceptionUpdateInfo& UpdateInfo)
{
	// Actor가 유효하지 않으면 무시한다.
	AActor* Actor = UpdateInfo.Target.Get();
	if (!Actor)
	{
		return;
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	if (UpdateInfo.Stimulus.WasSuccessfullySensed())
	{
		// 사망한 대상은 다시 잡지 않는다(죽음 알림이 비운 타겟의 재획득 방지).
		if (UVBAbilitySystemStatics::IsActorDead(Actor))
		{
			return;
		}

		if (!AcceptsStimulusFrom(UpdateInfo.Stimulus, *Actor))
		{
			return;
		}

		// 감지 성공 -> Blackboard에 타겟 설정. 잃었다가 다시 본 대상이면 잊기 타이머를 거둔다.
		if (LostTarget.Get() == Actor)
		{
			CancelLostTargetMemory();
		}

		BB->SetValueAsObject(TargetActorKeyName, Actor);
		VB_LOG(Log, "AI 감지: %s → Target = %s", *GetName(), *Actor->GetName());
	}
	else if (BB->GetValueAsObject(TargetActorKeyName) == Actor)
	{
		// 다른 감각이 아직 감지 중이면 잃은 것이 아니다.
		if (IsSensingNow(*Actor))
		{
			return;
		}
		// 바로 잊지 않고 SightMaxAge 동안 기억한다. 시야 감각은 만료 때 다시 알리지 않으므로 직접 타이머를 건다.
		//   기억 시간 0 = 잊지 않음(엔진과 같은 뜻).
		const float MaxAge = SightConfig ? SightConfig->GetMaxAge() : FAIStimulus::NeverHappenedAge;
		LostTarget = Actor;
		if (MaxAge >= FAIStimulus::NeverHappenedAge)
		{
			GetWorldTimerManager().ClearTimer(LostTargetTimerHandle);
			VB_LOG(Log, "AI 시야 잃음: %s → %s 를 잊지 않는다(기억 시간 0 = 무한)", *GetName(), *Actor->GetName());
			return;
		}

		GetWorldTimerManager().SetTimer(LostTargetTimerHandle, this, &AVBAIController::ForgetLostTarget, MaxAge, false);
		VB_LOG(Log, "AI 시야 잃음: %s → %s 를 %.1f초 기억한다", *GetName(), *Actor->GetName(), MaxAge);
	}
}

void AVBAIController::ForgetLostTarget()
{
	AActor* Lost = LostTarget.Get();
	LostTarget.Reset();
	UBlackboardComponent* BB = GetBlackboardComponent();
	// 그 사이 다른 대상으로 바뀌었거나(사망 통지, 새 감지) 다시 보이면 잊지 않는다.
	if (!Lost || !BB || BB->GetValueAsObject(TargetActorKeyName) != Lost || IsSensingNow(*Lost))
	{
		return;
	}

	BB->ClearValue(TargetActorKeyName);
	VB_LOG(Log, "AI 감지 해제: %s → Target cleared (%s 를 기억 시간 동안 다시 못 봤다)", *GetName(), *Lost->GetName());
}

bool AVBAIController::AcceptsStimulusFrom(const FAIStimulus& Stimulus, const AActor& Source) const
{
	// 시야는 이미 소속으로 거르므로 피격 감각만 여기서 본다.
	return Stimulus.Type != UAISense::GetSenseID<UAISense_Damage>()
		|| GetTeamAttitudeTowards(Source) == ETeamAttitude::Hostile;
}

bool AVBAIController::IsSensingNow(AActor& Actor)
{
	UAIPerceptionComponent* Perception = GetPerceptionComponent();
	FActorPerceptionBlueprintInfo Info;
	if (!Perception || !Perception->GetActorsPerception(&Actor, Info))
	{
		return false;
	}

	return Info.LastSensedStimuli.ContainsByPredicate([](const FAIStimulus& Stimulus) { return Stimulus.WasSuccessfullySensed(); });
}

void AVBAIController::HandleActorDied(FGameplayTag Channel, const FVBDeathFacts& Facts)
{
	// 버스는 게임 인스턴스 전역이라 다른 월드는 거른다. 블랙보드는 서버 권위다.
	if (!Facts.DeadActor || Facts.DeadActor->GetWorld() != GetWorld() || GetNetMode() == NM_Client)
	{
		return;
	}

	ClearTargetIfItIs(Facts.DeadActor);
}

void AVBAIController::ClearTargetIfItIs(const AActor* DeadActor)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (BB && DeadActor && BB->GetValueAsObject(TargetActorKeyName) == DeadActor)
	{
		BB->ClearValue(TargetActorKeyName);
		VB_LOG(Log, "사망 통지: %s 가 노리던 %s 를 놓는다", *GetName(), *DeadActor->GetName());
	}
}

void AVBAIController::PushAttackStartDistance(float Distance)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	if (BB->SetValue<UBlackboardKeyType_Float>(AttackRangeKeyName, Distance))
	{
		bReportedAttackRangeWriteFailure = false;
		return;
	}

	VB_CLOG(!bReportedAttackRangeWriteFailure, Warning,
		"Attack range blackboard write failed: %s key '%s' must be a writable Float",
		*GetName(), *AttackRangeKeyName.ToString());
	bReportedAttackRangeWriteFailure = true;
}
