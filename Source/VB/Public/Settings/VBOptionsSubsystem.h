// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Settings/VBOptionsDraft.h"
#include "VBOptionsSubsystem.generated.h"

class APlayerController;

DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnUserSettingsChanged, const FVBSettingsSaveData&);
DECLARE_DELEGATE_OneParam(FVBDifficultyRequest, FGameplayTag);

// 옵션이 기기 설정에 닿는 동작. 기본값은 엔진 UGameUserSettings 이고, 화면 시험은 엔진, 디스크에 닿지 않는 것으로 바꿔 끼운다.
struct VB_API FVBOptionsMachineActions
{
	// 초안을 걸고 저장한다. 화면 모드를 미리 보기로 걸었으면 true.
	TFunction<bool(const FVBMachineSettings&)> Commit;
	// 미리 보기 화면 모드를 유지(true)하거나 되돌린다(false).
	TFunction<void(bool)> DecideVideoMode;
	TFunction<void()> RunAutoDetect;

	static FVBOptionsMachineActions Engine();
};

/**
 * 옵션을 바꾸는 유일한 경로이자 엔진에 적용하는 담당 클래스. 사용자 설정은 바로잡기 -> 저장 -> 엔진에 적용 -> 알림.
 * 기기 설정은 UGameUserSettings 에 전달하고, 해상도, 창 모드가 바뀌면 유지/되돌리기를 호출자가 묻는다.
 */
UCLASS()
class VB_API UVBOptionsSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UVBOptionsSubsystem* Get(const ULocalPlayer* LocalPlayer);

	FVBOptionsDraft MakeDraft() const;
	// 기본값 한 벌(사용자 = 구조체 기본값, 기계 = 엔진 기본값 + 바탕화면 해상도). 기본값 단추가 쓴다.
	FVBOptionsDraft MakeDefaultDraft() const;

	// 초안을 확정한다. 해상도, 창 모드가 바뀌었으면 저장하지 않은 미리 보기로 걸고 참을 돌려준다.
	// 미리 보기 기한은 이 서브시스템이 재서, 유지가 없으면 화면이 사라져도 되돌린다.
	bool Commit(const FVBOptionsDraft& Draft);
	void KeepVideoMode();
	void RevertVideoMode();
	bool HasPendingVideoMode() const { return PendingVideoDeadline > 0.0; }
	double GetPendingVideoSecondsLeft() const;

	// 하드웨어 벤치마크로 그래픽 단계를 정하고 바로 건다.
	void RunAutoDetect();

	// 고를 수 있는 해상도(지금 해상도 포함, 작은 것부터).
	TArray<FIntPoint> GetSelectableResolutions() const;

	// 업스케일링 선택지 = 엔진 해상도 프리셋.
	TArray<FVBResolutionPresetChoice> GetResolutionPresets() const;

	FVBOnUserSettingsChanged OnUserSettingsChanged;

	// 난이도 변경을 서버로 보내는 연결. 플레이어 컨트롤러가 BeginPlay 에서 잇고 EndPlay 에서 끊는다.
	FVBDifficultyRequest DifficultyRequest;

	// 컨트롤러의 진동 문 둘(일반 진동, 햅틱)을 같이 열거나 닫는다.
	static void ApplyVibrationTo(APlayerController& Controller, bool bEnabled);

	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;
	virtual void Deinitialize() override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBConfirmationScreensTestAccess;
#endif

	void ApplyUserSettings(const FVBSettingsSaveData& Settings);
	void ApplyVolumes(const FVBSettingsSaveData& Settings);
	void ApplyVibration(const FVBSettingsSaveData& Settings) const;
	void FinishVideoPreview(bool bKeep);
	bool TickPendingVideo(float DeltaTime);
	void ClearPendingVideo();

	FVBOptionsMachineActions MachineActions = FVBOptionsMachineActions::Engine();

	// 믹스는 오디오 장치마다 한 번만 민다(미는 횟수가 장치의 참조 수다). 맵이 바뀌어도 장치가 같으면 덮기만 한다.
	TSet<uint32> MixPushedDevices;

	double PendingVideoDeadline = 0.0;
	FTSTicker::FDelegateHandle PendingVideoTicker;

	// UI 크기는 엔진 설정 CDO 를 바꾼다. 처음 값을 들고 있다가 내려갈 때 돌려놓는다(에디터 PIE 가 끝나도 남지 않게).
	TOptional<float> OriginalApplicationScale;
};
