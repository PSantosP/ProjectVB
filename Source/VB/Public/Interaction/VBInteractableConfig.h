// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/VBReachRules.h"
#include "Data/VBMoveCancelGate.h"   // FVBMoveCancelGate - 상호작용과 공격 회복 끊기가 같이 쓴다
#include "VBInteractableConfig.generated.h"

// 입력 방식. GDD 14장 7.1 의 세 종류.
UENUM(BlueprintType)
enum class EVBInteractMode : uint8
{
	Tap, // 누르면 즉시 (문, 스위치, 아이템)
	Hold, // HoldDuration 동안 누르고 있어야 완료 (레버, 밸브)
	// 입력 없이 InteractRange 안에 들어오면 한 번(저장 포인트). 거리만 보고 프롬프트 초점 후보에서 빠진다.
	Auto,
	Push, // E로 잡기/놓기, 잡은 동안 이동 입력을 대상이 소비한다.
};

class UAnimMontage;
class UAnimSequence;

// 접촉 시간은 몽타주 초, 목표는 소품 뼈/소켓 기준 손 뼈 변환이다. 이름이 비면 IK를 쓰지 않는다.
USTRUCT(BlueprintType)
struct VB_API FVBEnvironmentHandContact
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact")
	FName PropBone;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact")
	FTransform HandTarget = FTransform::Identity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact", meta=(ClampMin="0"))
	float StartTime = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact", meta=(ClampMin="0"))
	float EndTime = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact", meta=(ClampMin="0"))
	float BlendTime = 0.15f;

	float GetWeight(float MontageTime) const;
};

/**
 * 캐릭터 동작이 있는 상호작용 한 벌 - 캐릭터 몽타주, 짝인 소품 애니, 캐릭터 정렬 위치(소품 루트 기준).
 * 팩 애니는 캐릭터와 소품이 같은 원점에서 저작됐고, 캐릭터 앞(+X)이 메시 +Y 라 기본 Yaw 가 90 이다.
 * 캐릭터 애니는 제자리라 설 자리까지는 GA 가 옮긴다(루트모션이 없어 모션워핑 불가).
 */
USTRUCT(BlueprintType)
struct FVBInteractionMotion
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UAnimMontage> CharacterMontage;

	// 소품이 같은 시각에 재생할 애니. 비어 있으면 소품은 움직이지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UAnimSequence> PropAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FTransform StandTransform = FTransform(FRotator(0.0f, 90.0f, 0.0f));

	// 설 자리 기준. 끄면 소품 액터 변환(문, 레버), 켜면 소품 위치 + 플레이어가 다가온 방향(X = 플레이어 -> 소품, 높이 = 발).
	// 방향이 없는 소품(바닥 아이템)은 켠다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	bool bStandFromApproach = false;

	// 몽타주 시작 후 상태를 확정할 시각. 0은 기존 동시 재생, 발차기는 발 접촉 시각.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction", meta=(ClampMin="0"))
	float CommitTime = 0.0f;

	// 확정 순간부터 소품 애니를 재생할 위치(초). 준비 구간을 건너뛰어 접촉을 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction", meta=(ClampMin="0"))
	float PropStartTime = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact")
	FVBEnvironmentHandContact LeftContact;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Contact")
	FVBEnvironmentHandContact RightContact;

	bool HasCharacterMotion() const { return CharacterMontage != nullptr; }
};

/**
 * 상호작용 대상 하나의 설정. UVBInteractableComponent 가 든다.
 * 같은 종류 대상이 값을 공유하도록 자산으로 두고, 배치마다 다른 것은 대상 액터 쪽에 둔다.
 */
UCLASS(BlueprintType)
class VB_API UVBInteractableConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	EVBInteractMode Mode = EVBInteractMode::Tap;

	// 프롬프트에 나오는 행동 이름(「열기」「당기기」). 대상이 SetActionTextOverride 로 바꿀 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FText ActionText;

	// Hold 일 때 완료까지 걸리는 시간(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction",
		meta=(ClampMin="0.1", EditCondition="Mode==EVBInteractMode::Hold"))
	float HoldDuration = 1.5f;

	// 플레이어 캐릭터 중심에서 대상 컴포넌트 위치까지 이 거리 안이어야 한다(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction", meta=(ClampMin="0.0"))
	float InteractRange = 200.0f;

	// 높이차, 방향각, 시야. 기준 방향은 카메라 방향이다(플레이어가 보고 있는 것을 고른다).
	// 시야 캡슐은 캐릭터 중심에서 대상 컴포넌트 위치로 쓴다 - 바닥 물건은 PlaceAboveRestingMesh 로 띄운다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FVBReachRules ReachRules;

	// 칼을 든 채 상호작용하면 수납했다가 끝나고 원래 무기로 돌아온다.
	// Hold 와 몽타주가 있는 Tap 에만 적용된다(몽타주 없는 Tap 은 수납, 복원이 같은 프레임에 겹친다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	bool bSheatheWeapon = true;

	// 손이 닿기 전이나 누르는 동안(Hold) 새로 움직이면 취소한다(문, 레버). 끄면 닿을 때까지 이동을 막는다(줍기).
	// 닿은 뒤에는 어느 쪽이든 움직이면 남은 동작을 섞어 끝낸다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	bool bCancelOnMove = true;

	// 싸우는 중(UVBCombatCoordinatorSubsystem::IsEngaged)이면 캐릭터 동작, 수납, 정렬 없이 바로 한다. Tap 만 받는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	bool bInstantWhileEngaged = false;

	// 손이 닿은 뒤 새로 움직이면 남은 캐릭터 동작을 이 시간(초)에 걸쳐 블렌드해 끝낸다. 종전 리터럴 0.2 보존.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion", meta=(ClampMin="0.0"))
	float PostContactBlendOutSeconds = 0.2f;

	// 캐릭터 동작을 건너뛰고 바로 할 것인가. Tap 이고 위 플래그가 켜졌고 싸우는 중일 때만.
	bool ShouldSkipMotion(bool bEngaged) const { return bEngaged && bInstantWhileEngaged && Mode == EVBInteractMode::Tap; }

	// 켜짐 상태가 꺼짐->켜짐(문 열기, 스위치 켜기) / 켜짐->꺼짐 으로 바뀔 때의 캐릭터 동작. 앞쪽.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion")
	FVBInteractionMotion ActivateMotion;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion")
	FVBInteractionMotion DeactivateMotion;

	// 뒤쪽에서 할 때(문). 캐릭터 몽타주가 비어 있으면 뒤쪽이 없는 대상이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion")
	FVBInteractionMotion ActivateMotionBack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion")
	FVBInteractionMotion DeactivateMotionBack;

	// 닫혀 있을 때 길을 막는 종류인가(문, 게이트). 배치가 아니라 종류에 두어 모든 배치가 같은 약속을 진다.
	// 지키는 쪽은 AVBToggleProp::ValidateSetup 이다. 스위치, 밸브는 거짓.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Passage")
	bool bBlocksPassage = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Side")
	bool bUseSidePlane = false;

	// 소품 루트 기준 평면. 법선 쪽이 Front이며 움직이는 문짝을 따라 돌지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Side", meta=(EditCondition="bUseSidePlane"))
	FVector SidePlaneOrigin = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Side", meta=(EditCondition="bUseSidePlane"))
	FVector SidePlaneNormal = FVector::YAxisVector;

	// 닫힌 문을 질주하며 밀어 여는 쪽에서만 사용. 비어 있으면 일반 열기를 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Kick")
	FVBInteractionMotion KickOpenMotion;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Kick")
	bool bKickFromFront = true;

	// 문 쪽으로 향하는 속도가 이 값 이상이어야 찬다(질주 키 필요). 0 이면 서서도 차고, 물러나는 중에는 못 찬다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Kick", meta=(ClampMin="0"))
	float KickMinSpeed = 250.0f;

	// 닫기 중 E 재입력. 1은 비활성, 배속은 누적하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion", meta=(ClampMin="1", ClampMax="3"))
	float FastCloseRate = 1.0f;

	// 열기 중 E 재입력 배율. FastCloseRate 의 짝 - 1은 비활성, 누적하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion", meta=(ClampMin="1", ClampMax="3"))
	float FastOpenRate = 1.0f;

	// 이번 동작(시작 때 켜져 있었으면 닫기, 아니면 열기)의 E 재입력 배속. 1 이면 가속 없음. 데이터가 깨졌으면 1.
	float ResolveFastRate(bool bTargetWasActivated) const;

	// 설 자리까지 옮기는 시간(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion", meta=(ClampMin="0.0"))
	float AlignDuration = 0.25f;

	// 정렬이 막히면 접촉하지 못한 채 소품 상태를 바꾸지 않는다(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction|Motion", meta=(ClampMin="0"))
	float AlignTolerance = 10.0f;

	// 선택 결과를 재검색 없이 전달할 고정 순서: 앞 켜기/끄기, 뒤 켜기/끄기, 발차기 열기.
	const FVBInteractionMotion* GetMotionByIndex(uint8 Index) const;
	uint8 GetMotionIndex(const FVBInteractionMotion& Motion) const;

	// 판정면 또는 가까운 설 자리로 앞뒤를 고른다. 해당 방향에 동작이 없으면 null. PropTransform = 소품 루트 월드 변환.
	const FVBInteractionMotion* SelectMotion(bool bActivating, const FTransform& PropTransform,
		const FVector& InteractorLocation, bool bSprinting = false,
		const FVector& Velocity = FVector::ZeroVector) const;

	// 소품 애니만 필요할 때(연결된 스위치가 문을 여는 경우처럼 사람이 없다). 앞쪽 것을 쓴다.
	const FVBInteractionMotion& GetFrontMotion(bool bActivating) const { return bActivating ? ActivateMotion : DeactivateMotion; }

	// 길(bBlocksPassage)은 양쪽에서 쓸 수 있어야 한다 - 판정면, 앞뒤 켜기, 끄기 넷, 제 쪽 설 자리. 빈 문자열 = 문제 없음.
	// 한쪽만 저작하면 반대편 사람이 닫힌 문 너머 설 자리로 가려다 막힌다.
	FString ValidatePassage() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
