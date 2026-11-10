// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractableComponent.h"

#include "Components/PrimitiveComponent.h"
#include "CoreLoop/VBServerWrite.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Interaction/VBInteractableConfig.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Interaction/VBInteractableSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UVBInteractableComponent::UVBInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

const UVBInteractableConfig* UVBInteractableComponent::GetConfig() const
{
	return Config ? Config.Get() : GetDefault<UVBInteractableConfig>();
}

void UVBInteractableComponent::PlaceAboveRestingMesh(const UPrimitiveComponent& RestingMesh)
{
	// 캡슐 아래 끝이 메시 윗면보다 조금 위에 오게 한다. 메시 자신은 시야 검사가 무시한다(대상 액터).
	constexpr float Margin = 2.0f;
	const FBoxSphereBounds& MeshBounds = RestingMesh.Bounds;
	const float Top = MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z;
	SetWorldLocation(FVector(MeshBounds.Origin.X, MeshBounds.Origin.Y, Top + GetConfig()->ReachRules.LineOfSightHalfHeight + Margin));
}

void UVBInteractableComponent::SetInteractionEnabled(bool bEnabled)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	bInteractionEnabled = bEnabled;
}

FText UVBInteractableComponent::GetActionText() const
{
	return ActionTextOverride.IsEmpty() ? GetConfig()->ActionText : ActionTextOverride;
}

void UVBInteractableComponent::SetActionTextOverride(const FText& InText)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	ActionTextOverride = InText;
}

void UVBInteractableComponent::SetActivated(bool bNewActivated, bool bInstant)
{
	if (!VBServerWrite::HasOwnerAuthority(*this) || bActivated == bNewActivated)
	{
		return;
	}

	bActivated = bNewActivated;
	// OnRep 은 권한에서 안 불린다. 싱글은 리슨 호스트라 손으로 알리지 않으면 소품이 안 움직인다.
	OnActivationChanged.Broadcast(bActivated, bInstant);
}

void UVBInteractableComponent::OnRep_Activated()
{
	OnActivationChanged.Broadcast(bActivated, /*bInstant=*/false);
}

FName UVBInteractableComponent::GetSaveKey() const
{
	return bSaveState ? GetObjectKey() : NAME_None;
}

FName UVBInteractableComponent::GetObjectKey() const
{
	if (!GetOwner())
	{
		return NAME_None;
	}

	return SaveId.IsNone() ? GetOwner()->GetFName() : SaveId;
}

FVBRuleContext UVBInteractableComponent::MakeRuleContext(const AActor* InteractingActor) const
{
	const APawn* Pawn = Cast<APawn>(InteractingActor);
	FVBRuleContext Context = FVBRuleContext::Make(this, Pawn ? Pawn->GetPlayerState() : nullptr);
	Context.Counterpart = GetOwner();
	return Context;
}

bool UVBInteractableComponent::PassesUseConditions(const AActor* InteractingActor) const
{
	return UseConditions.IsEmpty() || VBRules::EvaluateAll(UseConditions, MakeRuleContext(InteractingActor));
}

void UVBInteractableComponent::ApplyRefused(AActor* InteractingActor)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	VB_LOG(Log, "상호작용 거절(조건 거짓): %s.%s <- %s", *GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InteractingActor));
	if (!RefusedEffects.IsEmpty())
	{
		VBRules::ApplyAll(RefusedEffects, MakeRuleContext(InteractingActor));
	}
}

bool UVBInteractableComponent::NotifyInteracted(AActor* InteractingActor)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return false;
	}
	// 완료 직전에 조건을 다시 본다. 완료 길 넷(탭 즉시, 동작 확정, 홀드, 자동)이 모두 여기로 모여서
	//   이 한 자리가 넷을 다 막는다. 시작 때 참이었어도 동작 도중에 거짓이 됐으면 완료 신호, 사건, 효과를 내지 않는다.
	if (!PassesUseConditions(InteractingActor))
	{
		VB_LOG(Log, "상호작용 완료 거절(조건 거짓): %s.%s <- %s", *GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InteractingActor));
		return false;
	}

	VB_LOG(Log, "상호작용 완료: %s.%s <- %s", *GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InteractingActor));
	ActionFailure.Reset();
	OnInteracted.Broadcast(InteractingActor);
	// 소유 액터의 동작(줍기 등)이 실패했으면 이 상호작용은 끝나지 않았다 - 사건, 효과가 실패한 동작을 성공으로 적지 않게.
	if (!ActionFailure.IsEmpty())
	{
		VB_CLOG(ActionFailure != LastReportedFailure, Warning, "상호작용 %s.%s: 동작 실패(%s) - 완료 사건 · 효과를 내지 않는다",
			*GetNameSafe(GetOwner()), *GetName(), *ActionFailure);
		LastReportedFailure = ActionFailure;
		ActionFailure.Reset();
		return false;
	}

	LastReportedFailure.Reset();

	const APawn* Pawn = Cast<APawn>(InteractingActor);
	APlayerState* InstigatorState = Pawn ? Pawn->GetPlayerState() : nullptr;

	// 퀘스트가 구독할 이벤트. EventTags 가 없는 대상(대부분의 문, 스위치)은 보내지 않는다.
	if (!EventTags.IsEmpty())
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			FVBGameEvent Event;
			Event.EventTag = VBGameplayTags::Event_Interaction_Completed;
			Event.TargetTags = EventTags;
			Event.Instigator = InstigatorState;
			Events->Broadcast(Event);
		}
	}

	// 완료 효과는 사건 뒤다 - "찬장을 열어라 -> 안의 약을 구하라" 에서 효과가 낸 아이템 사건이 먼저 가면
	//   둘째 단계가 서기 전에 지나가 버린다.
	if (!CompletionEffects.IsEmpty())
	{
		const FVBRuleContext Context = MakeRuleContext(InteractingActor);
		const VBRules::EApplyResult Applied = VBRules::ApplyAllUntilFailure(CompletionEffects, [&Context](FVBRuleContext& Out)
		{
			Out = Context;
			return Out.CanApplyEffects();
		});
		// 효과가 해내지 못했으면(사실 저장소 없음 등) 완료로 치지 않는다 - 일회용 소품이 사실 없이 잠기지 않게.
		if (Applied != VBRules::EApplyResult::Landed)
		{
			VB_LOG(Warning, "상호작용 %s.%s: 완료 효과가 해내지 못했다 - 완료로 치지 않는다", *GetNameSafe(GetOwner()), *GetName());
			return false;
		}
	}

	OnCompletionApplied.Broadcast(InteractingActor);
	return true;
}

void UVBInteractableComponent::ReportActionFailed(const FString& Reason)
{
	ActionFailure = Reason.IsEmpty() ? TEXT("이유 없음") : Reason;
}

void UVBInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	// 선택적 컴포넌트가 비활성/미설정이면 후보 등록도 하지 않는다.
	if (!Config && !bInteractionEnabled)
	{
		return;
	}

	// 미할당은 동작한다(CDO 기본값) - 그래서 조용히 넘어가면 문 하나가 다른 문과 다르게 구는 것으로만 보인다.
	if (!Config)
	{
		VB_LOG(Warning, "상호작용 대상 %s.%s 에 Config 가 없다 - 기본값(탭, 200cm, 문구 없음)으로 동작한다",
			*GetNameSafe(GetOwner()), *GetName());
	}

	if (UVBInteractableSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UVBInteractableSubsystem>() : nullptr)
	{
		Subsystem->Register(this);
	}
}

void UVBInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBInteractableSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UVBInteractableSubsystem>() : nullptr)
	{
		Subsystem->Unregister(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UVBInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBInteractableComponent, bInteractionEnabled);
	DOREPLIFETIME(UVBInteractableComponent, ActionTextOverride);
	DOREPLIFETIME(UVBInteractableComponent, bActivated);
}

bool UVBInteractableComponent::SharesSaveKey(const UVBInteractableComponent& A, const UVBInteractableComponent& B)
{
	const FName Key = A.GetSaveKey();
	return &A != &B && !Key.IsNone() && B.GetSaveKey() == Key;
}

#if WITH_EDITOR
EDataValidationResult UVBInteractableComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<const AActor*> SeenParts;
	for (const TObjectPtr<AActor>& Part : TargetParts)
	{
		if (!IsValid(Part.Get()) || Part.Get() == GetOwner() || Part->GetWorld() != GetWorld() || SeenParts.Contains(Part.Get()))
		{
			Context.AddError(NSLOCTEXT("VBInteractable", "InvalidTargetParts",
				"TargetParts 에 비어 있거나 자신, 다른 월드, 중복된 액터가 있다. 같은 대상의 물리 조각만 지정한다."));
			Result = EDataValidationResult::Invalid;
		}
		SeenParts.Add(Part.Get());
	}
	const FName Key = GetSaveKey();
	const UWorld* World = GetWorld();
	if (Key.IsNone() || !World)
	{
		return Result;
	}
	// 켜짐 저장은 월드 하나의 "이름표 -> 켜짐" 표다(UVBInteractableSubsystem::CollectSavedStates) - 같은 이름표 둘은 한 칸을 덮어쓴다.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UVBInteractableComponent*> Others(*It);
		for (const UVBInteractableComponent* Other : Others)
		{
			if (Other && SharesSaveKey(*this, *Other))
			{
				Context.AddError(FText::Format(NSLOCTEXT("VBInteractable", "DuplicateSaveKey",
					"{0}: 저장 이름표 {1} 를 {2} 도 쓴다 - 두 대상이 저장된 켜짐 하나를 나눠 쓴다. 한쪽에 SaveId 를 따로 적는다."),
					FText::FromString(GetNameSafe(GetOwner())), FText::FromName(Key), FText::FromString(GetNameSafe(Other->GetOwner()))));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#endif
