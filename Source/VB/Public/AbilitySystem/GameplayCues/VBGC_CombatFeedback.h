// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_CombatFeedback.generated.h"

class APawn;
class UVBCombatFeedbackProfile;
enum class EVBFeedbackRecipient : uint8;

// 연출 한 번의 입력. 큐 파라미터에서 뽑아 PlayFeedback 에 전달한다.
struct FVBCombatFeedbackEvent
{
	const UVBCombatFeedbackProfile* Profile = nullptr;
	// 화면, 슬로우를 받을 폰. 이 머신에서 로컬 조종일 때만 받는다.
	APawn* Recipient = nullptr;
	FVector Location = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector; // 접촉 법선. 0 이면 없음
	FVector Direction = FVector::ZeroVector; // 사건 방향(스윙). 0 이면 없음
	// 접점이 없는 사건(스윙당 한 번)은 끈다.
	bool bWorldEffects = true;
	// 화면 층 세기 배율. 스윙이면 무기의 SwingShakeScale, 방어 사건은 1.
	float ScreenScale = 1.0f;
};

/**
 * 전투 연출 큐 - UVBCombatFeedbackProfile 하나를 접점에서 재생한다. 방어 성공 시 GCN_Parry / GCN_GuardBlock 이 쏜다.
 * 스윙 강타는 PlayFeedback 을 직접 부른다. 태그는 BP 자식이 든다(네이티브 생성자에 두면 등록이 빠진다). 상태 없음.
 */
UCLASS()
class VB_API UVBGC_CombatFeedback : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	// 이 큐가 재생하는 연출 자산. 시험이 「이 큐가 지금 무엇을 띄우나」를 자산 경로 없이 묻는 곳이다.
	const UVBCombatFeedbackProfile* GetProfile() const { return Profile; }

	// 선택된 Pawn이 같은 월드에 있을 때만 화면 수신자로 돌려준다.
	static APawn* ResolveScreenRecipient(AActor* Target, const FGameplayCueParameters& Parameters, EVBFeedbackRecipient Recipient);

	// 월드 FX + 로컬 수신자의 슬로우, 화면. 화면 충격을 걸었으면 true. 데디 서버에서는 아무것도 안 한다.
	static bool PlayFeedback(UWorld* World, const FVBCombatFeedbackEvent& Event, const FString& LogName);

protected:
	// 비면 경고하고 아무것도 안 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback")
	TObjectPtr<const UVBCombatFeedbackProfile> Profile;

	// 접점이 받는 캐릭터 캡슐 안이면 공격자 쪽 표면에서 이만큼(cm) 바깥으로 민다. 0 이면 안 민다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback", meta=(ClampMin="0", Units="cm"))
	float ContactStandOff = 30.0f;

	// 민 접점의 높이 = 캡슐 중심 + 반높이 x 이 값(0.35 = 가슴 앞).
	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback", meta=(ClampMin="-1", ClampMax="1"))
	float ContactHeightRatio = 0.35f;
};
