// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBFinisherStrikeFeedback.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBCombatFeedbackProfile.h"
#include "Data/VBFinisherConfig.h"
#include "Data/VBSwingCombatProfile.h"
#include "GameFramework/Character.h" // 처형 대상의 바디 메시 - 판정에 안 닿은 타격의 가장 가까운 점
#include "GameplayCueInterface.h" // FGameplayCueParameters
#include "VB/VB.h"

namespace
{
	// 판정에 안 닿은 타격의 가까운 점을 찾을 때 날 위에서 재는 점 수.
	constexpr int32 BladeSampleCount = 5;

	// 지금 그 모양의 날 시작, 끝(월드). 판정 컴포넌트와 같은 풀이다.
	bool GetStrikeBlade(const FVBFinisherStrikeContext& Context, const FGameplayTag& Shape, FVector& OutBase, FVector& OutTip)
	{
		const UVBMeleeTraceComponent* Trace = UVBMeleeTraceComponent::FindFor(Context.Avatar);
		const USkeletalMeshComponent* Mesh = Trace ? Trace->GetMesh() : nullptr;
		FVBResolvedMeleeShape Resolved;
		if (!Mesh || !Trace->ResolveShape(Shape, Resolved))
		{
			return false;
		}

		const FTransform Bone = Mesh->GetSocketTransform(Resolved.Bone, RTS_World);
		OutBase = Bone.TransformPosition(Resolved.BaseLocal);
		OutTip = Bone.TransformPosition(Resolved.TipLocal);
		return true;
	}
}

bool FVBFinisherStrikeFeedback::TryBeginListening()
{
	if (bListening)
	{
		return false;
	}

	bListening = true;
	return true;
}

void FVBFinisherStrikeFeedback::OpenStrike(const FGameplayEventData& Payload, const FVBFinisherStrikeContext& Context,
	TFunctionRef<FVBMeleeTraceHits(int32)> MakeHitCallback)
{
	if (!Context.bSceneActive || !Context.Target)
	{
		return;
	}
	// 이전 한 프레임 노티에는 Melee.Window 태그가 없다. 처형 몽타주는 창으로만 저작한다 - 태그 없는 이벤트는 처형 타격이 아니다.
	if (!Payload.InstigatorTags.HasTagExact(VBGameplayTags::Melee_Window))
	{
		return;
	}
	// 처형 타격은 날 하나씩이다. 여러 부위 노티는 첫 모양만 쓰고 경고한다.
	const FGameplayTagContainer Shapes = UVBMeleeTraceComponent::ShapesFromEventTags(Payload.InstigatorTags);
	const FGameplayTag ShapeTag = Shapes.IsEmpty() ? FGameplayTag() : Shapes.First();
	if (Shapes.Num() > 1)
	{
		VB_LOG(Warning, "%s: 처형 타격 창에 모양이 %d 개 (%s) - 처형은 날 하나만 쓴다, 첫 모양 %s 로 연다", *GetNameSafe(Context.Ability), Shapes.Num(),
			*Shapes.ToStringSimple(), *ShapeTag.ToString());
	}

	UVBMeleeTraceComponent* Trace = UVBMeleeTraceComponent::FindFor(Context.Avatar);
	FVBResolvedMeleeShape Shape;
	// 못 여는 타격은 피가 조용히 사라지므로 경고한다.
	if (!Trace || !ShapeTag.IsValid() || !Trace->ResolveShape(ShapeTag, Shape))
	{
		VB_LOG(Warning, "%s: 처형 타격 창을 못 연다 - %s", *GetNameSafe(Context.Ability),
			!Trace ? TEXT("아바타에 UVBMeleeTraceComponent 가 없다")
			: !ShapeTag.IsValid() ? TEXT("노티에 모양 태그가 없다") : TEXT("지금 무기에서 그 모양이 안 풀린다"));
		return;
	}

	for (int32 Slot = 0; Slot < Strikes.Num(); ++Slot)
	{
		if (Strikes[Slot].WindowId != INDEX_NONE && Strikes[Slot].Shape == ShapeTag)
		{
			CloseStrike(Slot, /*bReachedEnd=*/true, Context);
		}
	}

	int32 Slot = Strikes.IndexOfByPredicate([](const FStrike& Strike) { return Strike.WindowId == INDEX_NONE; });
	if (Slot == INDEX_NONE)
	{
		Slot = Strikes.AddDefaulted();
	}

	FStrike& Strike = Strikes[Slot];
	Strike.Shape = ShapeTag;
	Strike.CombatProfile = Cast<UVBSwingCombatProfile>(Payload.OptionalObject2.Get());
	// 처형 설정의 큐 > 판정 모양의 큐 > 능력의 기본 큐. 처형 설정이 큐를 가지면 평타와 따로 고칠 수 있다.
	Strike.CueTag = (Context.Config && Context.Config->StrikeCueTag.IsValid()) ? Context.Config->StrikeCueTag
		: Shape.ImpactCueTag.IsValid() ? Shape.ImpactCueTag : Context.FallbackCue;
	Strike.bFired = false;
	FVector Base, Tip;
	Strike.BeginBladeCenter = GetStrikeBlade(Context, ShapeTag, Base, Tip) ? (Base + Tip) * 0.5 : FVector::ZeroVector;
	// 재접촉 간격은 노티가 싣는다(UVBANS_AttackTraceWindow::RehitInterval, 기본 0 = 창 하나에 대상당 한 번 보고).
	Strike.WindowId = Trace->BeginWindow(ShapeTag, Payload.EventMagnitude, MakeHitCallback(Slot));
}

void FVBFinisherStrikeFeedback::CloseStrikes(const FGameplayEventData& Payload, const FVBFinisherStrikeContext& Context)
{
	for (int32 Slot = 0; Slot < Strikes.Num(); ++Slot)
	{
		if (Strikes[Slot].WindowId != INDEX_NONE && Payload.InstigatorTags.HasTagExact(Strikes[Slot].Shape))
		{
			CloseStrike(Slot, /*bReachedEnd=*/Payload.EventMagnitude > 0.0f, Context);
		}
	}
}

void FVBFinisherStrikeFeedback::CloseStrike(int32 Slot, bool bReachedEnd, const FVBFinisherStrikeContext& Context)
{
	if (UVBMeleeTraceComponent* Trace = UVBMeleeTraceComponent::FindFor(Context.Avatar))
	{
		// 닫기가 마지막 구간을 검사하며 ReportHits 를 부를 수 있다 - 배열은 그 안에서 안 늘어난다.
		Trace->EndWindow(Strikes[Slot].WindowId);
	}

	FStrike& Strike = Strikes[Slot];
	Strike.WindowId = INDEX_NONE;
	if (!bReachedEnd || Strike.bFired)
	{
		return;
	}
	// 끝까지 안 닿았다. 날에서 처형 대상 메시(물리 바디)까지 가장 가까운 점을 찾아, 가까우면 거기에 한 번 낸다.
	const ACharacter* TargetCharacter = Cast<ACharacter>(Context.Target);
	USkeletalMeshComponent* Body = TargetCharacter ? TargetCharacter->GetMesh() : nullptr;
	FVector Base, Tip;
	float Best = -1.0f;
	FVector BestBlade = FVector::ZeroVector;
	if (Body && Context.NearMissReach > 0.0f && GetStrikeBlade(Context, Strike.Shape, Base, Tip))
	{
		FindClosestBladePoint(Base, Tip, [Body](const FVector& Point)
		{
			FVector OnBody;
			return Body->GetClosestPointOnCollision(Point, OnBody); // 바디 안이면 0, 못 구하면 음수
		}, Best, BestBlade);
	}

	if (Best >= 0.0f && Best <= Context.NearMissReach)
	{
		// 큐가 읽는 모양으로 꾸민다: Location = 날이 있던 자리(큐가 거기서 메시 표면을 다시 찾는다), TraceStart->End = 날이 움직인 방향.
		FHitResult Hit(Context.Target, Body, BestBlade, FVector::ZeroVector);
		Hit.Location = BestBlade;
		Hit.TraceStart = Strike.BeginBladeCenter.IsNearlyZero() ? BestBlade : Strike.BeginBladeCenter;
		Hit.TraceEnd = (Base + Tip) * 0.5;
		VB_LOG(Log, "%s: 처형 타격 창(%s)이 판정에 안 닿아 가장 가까운 점(%.1fcm)에 냈다", *GetNameSafe(Context.Ability), *Strike.Shape.ToString(), Best);
		FireStrike(Slot, Hit, Context);
		return;
	}
	// 저작한 타격이 캐릭터에서 멀리 지나갔다(창 시각이나 정렬이 어긋남) - 경고한다.
	VB_LOG(Warning, "%s: 처형 타격 창(%s)이 %s 에 안 닿고 닫혔다(가장 가까운 거리 %.1fcm) - 이 타격은 피·소리가 없다", *GetNameSafe(Context.Ability),
		*Strike.Shape.ToString(), *GetNameSafe(Context.Target), Best);
}

bool FVBFinisherStrikeFeedback::FindClosestBladePoint(const FVector& Base, const FVector& Tip,
	TFunctionRef<float(const FVector&)> DistanceTo, float& OutDistance, FVector& OutPoint)
{
	bool bFound = false;
	for (int32 Index = 0; Index < BladeSampleCount; ++Index)
	{
		const FVector Point = FMath::Lerp(Base, Tip, Index / float(BladeSampleCount - 1));
		const float Distance = DistanceTo(Point);
		if (Distance >= 0.0f && (!bFound || Distance < OutDistance))
		{
			bFound = true;
			OutDistance = Distance;
			OutPoint = Point;
		}
	}

	return bFound;
}

void FVBFinisherStrikeFeedback::End(const FVBFinisherStrikeContext& Context)
{
	// 노티의 끝 알림이 몽타주 중단에서도 오지만, 어빌리티 종료가 먼저 오면 여기가 닫는다.
	for (int32 Slot = 0; Slot < Strikes.Num(); ++Slot)
	{
		if (Strikes[Slot].WindowId != INDEX_NONE)
		{
			CloseStrike(Slot, /*bReachedEnd=*/false, Context);
		}
	}

	Strikes.Reset();
	bListening = false;
}

void FVBFinisherStrikeFeedback::ReportHits(const TArray<FHitResult>& Hits, int32 Slot, const FVBFinisherStrikeContext& Context)
{
	if (!Context.Target || !Context.OwnerASC || !Strikes.IsValidIndex(Slot) || Strikes[Slot].bFired)
	{
		return;
	}

	for (const FHitResult& Hit : Hits)
	{
		// 장면 안의 그 캐릭터만. 옆에 선 다른 적을 날이 스쳐도 처형 연출로 피를 내지 않는다.
		if (Hit.GetActor() != Context.Target)
		{
			continue;
		}
		// 창 하나 = 타격 하나: 처음 닿음만 쓴다. 메시 안에 머무는 동안 재접촉 간격마다 들어오는 보고는 버린다.
		FireStrike(Slot, Hit, Context);
		return;
	}
}

void FVBFinisherStrikeFeedback::FireStrike(int32 Slot, const FHitResult& Hit, const FVBFinisherStrikeContext& Context)
{
	UAbilitySystemComponent* OwnerASC = Context.OwnerASC;
	if (!OwnerASC)
	{
		return;
	}

	FStrike& Strike = Strikes[Slot];
	Strike.bFired = true;
	// 콤보의 "판정 창 N 접촉" 로그와 짝 - 없으면 다 터진 것과 창이 안 열린 것을 못 가른다.
	VB_LOG(Log, "%s: 처형 타격 - %s 창이 %s 의 %s 에 닿았다 (큐 %s)", *GetNameSafe(Context.Ability), *Strike.Shape.ToString(),
		*GetNameSafe(Hit.GetActor()), *Hit.BoneName.ToString(), *Strike.CueTag.ToString());
	// 일반 타격과 같은 문맥: HitResult 를 실어 큐가 접점, 법선, 날 방향을 꺼낸다.
	FGameplayEffectContextHandle EffectContext = OwnerASC->MakeEffectContext();
	EffectContext.AddHitResult(Hit);
	FGameplayCueParameters CueParams;
	CueParams.EffectContext = EffectContext;
	const UVBSwingCombatProfile* CombatProfile = Strike.CombatProfile.Get();
	OwnerASC->ExecuteGameplayCue(Strike.CueTag, CueParams);
	// 처형 타격은 항상 진짜 타격이라 맞은 쪽 플래시도 같은 자리에 낸다. 처형 피해 자체는 접점이 없어 플래시가 없다.
	if (UAbilitySystemComponent* StruckASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor()))
	{
		StruckASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitFlash, CueParams);
	}

	// 노티의 창별 강조를 살려 기존 화면 큐를 쓴다. 접촉 FX 는 피 큐 몫이고 피해, 히트스톱은 없다.
	if (Context.bSceneActive && CombatProfile && CombatProfile->ScreenFeedback)
	{
		FGameplayEffectContextHandle ScreenContext = OwnerASC->MakeEffectContext();
		ScreenContext.AddSourceObject(CombatProfile->ScreenFeedback.Get());
		ScreenContext.AddHitResult(Hit);
		FGameplayCueParameters ScreenParams;
		ScreenParams.EffectContext = ScreenContext;
		ScreenParams.Instigator = const_cast<AActor*>(Context.Avatar);
		ScreenParams.Normal = (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal();
		OwnerASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_SwingImpact, ScreenParams);
	}
}
