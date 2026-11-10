#include "Character/VBGuardComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBParryConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "VB/VB.h"

UVBGuardComponent::UVBGuardComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UVBGuardComponent* UVBGuardComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBGuardComponent>() : nullptr;
}

bool UVBGuardComponent::HasOwnerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

UAbilitySystemComponent* UVBGuardComponent::FindOwnerAbilitySystem() const
{
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
}

UAnimInstance* UVBGuardComponent::FindOwnerAnimInstance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
}

const UVBParryConfig* UVBGuardComponent::GetParryConfig() const
{
	return ParryConfig ? ParryConfig.Get() : GetDefault<UVBParryConfig>();
}

float UVBGuardComponent::GetGuardElapsed() const
{
	const UWorld* World = GetWorld();
	if (!IsGuarding() || !World)
	{
		return TNumericLimits<float>::Max();
	}

	return World->GetTimeSeconds() - GuardStartServerTime;
}

bool UVBGuardComponent::BeginGuard()
{
	// 시작 시각은 데미지 판정이 읽는 서버 진실이다 - 호출자의 규율에 기대지 않고 여기서 막는다.
	if (!HasOwnerAuthority())
	{
		return false;
	}
	// 다시 시작하지 않는다: 몽타주가 Start 로 되감기고, 시작 시각이 새로 찍혀 패링 창이 매번 갓 열린 것이 된다(블록이 영영 안 뜬다).
	if (IsGuarding())
	{
		return true;
	}

	// 세트는 손에 든 무기의 정의에 있다. 고른 정의가 없으면 아무것도 커밋하지 않는다 - 시작 시각만 찍히면
	//   몽타주 없는 영구 가드가 되어 데미지 판정이 계속 막는다. 다른 무기 것으로 내려가는 폴백은 없다.
	const UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(GetOwner());
	const UVBWeaponDefinition* Weapon = Weapons ? Weapons->GetSelectedDefinition() : nullptr;
	if (!Weapon)
	{
		VB_LOG(Warning, "%s: guard refused - no weapon definition in hand (selected %s). The guard ability ends.",
			*GetNameSafe(GetOwner()), Weapons ? *UEnum::GetValueAsString(Weapons->GetCurrentWeaponType()) : TEXT("no weapon state"));
		return false;
	}

	const FVBGuardSet& Set = Weapon->GetGuardSet();

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	GuardStartServerTime = Now;
	// 잠금 확인보다 먼저 고정한다: 받아넘김 중에 다시 누른 가드(탭 패링)도 PlayGuardAccept 가 받아넘김 몽타주를 써야 한다.
	ActiveGuardMontage = Set.GuardMontage;
	ActiveParryAcceptMontage = Set.ParryAcceptMontage;
	ActiveParryAcceptMontageRight = Set.ParryAcceptMontageRight;
	ActiveGuardEndSection = Set.GuardEndSection;

	// 받아넘김 재생 중이면 가드 몽타주를 재생하지 않는다. 가드 상태는 위에서 섰으니 판정은 그대로다.
	if (Now < GuardMontageLockUntil || !ActiveGuardMontage)
	{
		return true;
	}

	UAnimInstance* AnimInstance = FindOwnerAnimInstance();
	if (!AnimInstance)
	{
		return true;
	}
	// 엔진은 재생 실패를 알리지 않아 경고를 남긴다 (가드 자체는 몽타주 없이도 선다)
	if (AnimInstance->Montage_Play(ActiveGuardMontage) <= 0.0f)
	{
		VB_LOG(Warning, "Guard montage '%s' failed to play (returned 0) - guard state is authoritative but has no animation.",
			*GetNameSafe(ActiveGuardMontage));
	}
	// 몽타주 자산에 자기 루프를 구울 수 없어 실행 중에 잇는다: Start -> Loop(무한) ... EndGuard 가 End 로 점프. End 는 next=None 이어야 끝나고 풀린다.
	//   섹션 이름은 몽타주와 같은 세트에서 읽는다 - 다른 무기의 섹션과 섞일 수 없다.
	AnimInstance->Montage_SetNextSection(Set.GuardStartSection, Set.GuardLoopSection, ActiveGuardMontage);
	AnimInstance->Montage_SetNextSection(Set.GuardLoopSection, Set.GuardLoopSection, ActiveGuardMontage);
	AnimInstance->Montage_SetNextSection(ActiveGuardEndSection, NAME_None, ActiveGuardMontage);
	return true;
}

void UVBGuardComponent::EndGuard()
{
	// MP 에서 소유 클라는 가드 연출을 못 본다 - 몽타주를 실어 나르는 복제가 없다(raw UAnimInstance 호출). 판정은 서버 진실이라 게임은 맞다.
	if (!HasOwnerAuthority())
	{
		return;
	}

	GuardStartServerTime = -1.0f;

	// 진입 때 재생한 그 몽타주의 End 로 가서 물러난 만큼 돌아온다 - 현재 무기로 다시 고르면 무기 교체 뒤 복귀가 빠진다
	// 가드 몽타주가 이미 꺼졌으면(받아넘김이 덮음) 그 몽타주가 자기 End 로 복귀한다
	if (ActiveGuardMontage)
	{
		UAnimInstance* AnimInstance = FindOwnerAnimInstance();
		if (AnimInstance && AnimInstance->Montage_IsActive(ActiveGuardMontage))
		{
			AnimInstance->Montage_JumpToSection(ActiveGuardEndSection, ActiveGuardMontage);
		}
	}

	ActiveGuardMontage = nullptr;
	ActiveParryAcceptMontage = nullptr;
	ActiveParryAcceptMontageRight = nullptr;
	ActiveGuardEndSection = NAME_None;
}

void UVBGuardComponent::ReleaseSession()
{
	// 연결이 끊길 때 가드 중이면 EndGuard 와 같이 끝낸다 - 캐시를 그냥 비우면 End 섹션 점프가 빠져 가드 진입 때 물러난 만큼 돌아오지 않는다.
	if (IsGuarding())
	{
		EndGuard();
	}

	GuardStartServerTime = -1.0f;
	ActiveGuardMontage = nullptr;
	ActiveParryAcceptMontage = nullptr;
	ActiveParryAcceptMontageRight = nullptr;
	ActiveGuardEndSection = NAME_None;
	GuardMontageLockUntil = 0.0f;
	CloseRiposteWindow();
}

void UVBGuardComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseSession();
	Super::EndPlay(EndPlayReason);
}

void UVBGuardComponent::PlayGuardAccept(EVBHitDirection Side)
{
	// 받아넘김 = 진입 때 무기의 받아넘김 몽타주(지금 재생 중인 가드 포즈와 짝). 재생 길이만큼 잠가서 다음 탭이 받아넘김을 자르지 않게 한다.
	//   하한은 설정값 - 재생이 0 을 돌려줘도 잠금이 바로 풀리지 않는다.
	FVBGuardSet Active;
	Active.ParryAcceptMontage = ActiveParryAcceptMontage;
	Active.ParryAcceptMontageRight = ActiveParryAcceptMontageRight;
	UAnimMontage* Accept = Active.PickParryAccept(Side);
	if (!Accept)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = FindOwnerAnimInstance())
	{
		const float Length = AnimInstance->Montage_Play(Accept);
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		GuardMontageLockUntil = Now + FMath::Max(Length, GetParryConfig()->MinGuardAcceptLockout);
	}
}

void UVBGuardComponent::OpenRiposteWindow(float Duration, AActor* Attacker)
{
	UAbilitySystemComponent* ASC = FindOwnerAbilitySystem();
	UWorld* World = GetWorld();
	if (!HasOwnerAuthority() || !ASC || !World)
	{
		return;
	}
	// 패링한 상대로 바로 락온, 바라보기. 대상이 정해진 액터라 카메라 탐색이 필요 없어 서버에서 건다.
	if (UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(GetOwner()))
	{
		Lock->FaceAndLockParryTarget(Attacker, GetParryConfig()->bAutoLockOnParry, GetParryConfig()->bFaceAttackerOnParry);
	}
	// 남아 있던 창도 닫는다 - 그 사이 반격을 잃었으면 옛 창이 경공격만 막는다.
	if (Duration <= 0.0f)
	{
		CloseRiposteWindow();
		return;
	}
	// 몫이 다른 ASC 에 있으면 그쪽에 먼저 돌려준다. 같은 ASC 면 더하지 않고 타이머만 새로 건다(먼저 만료된 타이머가 유효한 창을 지우지 않게).
	if (bRiposteWindowTagHeld && RiposteWindowASC.Get() != ASC)
	{
		CloseRiposteWindow();
	}

	World->GetTimerManager().ClearTimer(RiposteWindowHandle);
	// 태그 유무가 아니라 이 캐릭터가 더한 몫을 센다 - 다른 캐릭터가 남긴 창이 있어도 자기 몫을 따로 더해야 닫을 때 자기 것만 뺀다.
	if (!bRiposteWindowTagHeld)
	{
		ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow, 1, EGameplayTagReplicationState::CountToOwner);
		bRiposteWindowTagHeld = true;
		RiposteWindowASC = ASC;
	}

	World->GetTimerManager().SetTimer(RiposteWindowHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { CloseRiposteWindow(); }),
		Duration, false);
}

void UVBGuardComponent::CloseRiposteWindow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RiposteWindowHandle);
	}

	if (!bRiposteWindowTagHeld)
	{
		return;
	}

	bRiposteWindowTagHeld = false;
	// 더한 ASC 에서 뺀다. 그 ASC 가 이미 없으면 뺄 곳이 없다 - 다른 ASC 를 찾아 남의 몫을 걷지 않는다.
	if (UAbilitySystemComponent* ASC = RiposteWindowASC.Get())
	{
		ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow, 1, EGameplayTagReplicationState::CountToOwner);
	}

	RiposteWindowASC.Reset();
}

void UVBGuardComponent::ApplyGuardStaminaDelta(float Delta)
{
	// 피격에 대한 반응이라 능력 비용(GE)이 아니다. 서버가 쓰고 속성 복제가 따라간다.
	UAbilitySystemComponent* ASC = FindOwnerAbilitySystem();
	if (!HasOwnerAuthority() || !ASC || FMath::IsNearlyZero(Delta))
	{
		return;
	}

	const float Current = ASC->GetNumericAttribute(UVBCombatAttributeSet::GetStaminaAttribute());
	const float Max = ASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxStaminaAttribute());
	ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetStaminaAttribute(), FMath::Clamp(Current + Delta, 0.0f, Max));
}
