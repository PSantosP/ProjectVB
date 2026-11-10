// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Narrative/Dialogue/VBSpokenLine.h"
#include "VBSpokenLineComponent.generated.h"

class APlayerState;

DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnSpokenLine, const FVBSpokenLine& /*Line*/, float /*Seconds*/);

/**
 * 한 줄을 그 플레이어의 화면으로 보낸다. 서버 효과에서 소유 클라로 가는 길이라 PlayerController 에 붙는다.
 * 그리는 것은 UVBSubtitleWidget(OnLineStarted)이고, 음성은 위젯이 없어도 나도록 여기서 튼다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBSpokenLineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBSpokenLineComponent();

	// 그 플레이어의 PC 에 붙은 것. 없으면 nullptr.
	static UVBSpokenLineComponent* FindFor(const APlayerState* PlayerState);

	// 서버에서 부른다. 소유 클라에서 OnLineStarted 가 온다(리슨 호스트면 같은 프레임).
	void SpeakLine(const FVBSpokenLine& Line);

	// 줄이 시작될 때(소유 클라). Seconds = 보일 시간.
	FVBOnSpokenLine OnLineStarted;

	// 글자 수로 보일 시간을 정할 때: 최소 + 글자당. 한국어 자막 읽기 속도 기준값이며 맞춰 볼 대상이다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Dialogue", meta=(ClampMin="0.5"))
	float MinLineSeconds = 1.8f;

	UPROPERTY(EditDefaultsOnly, Category="VB|Dialogue", meta=(ClampMin="0.0"))
	float SecondsPerCharacter = 0.08f;

	float ResolveSeconds(const FVBSpokenLine& Line) const;

private:
	UFUNCTION(Client, Reliable)
	void ClientSpeakLine(const FVBSpokenLine& Line);
};
