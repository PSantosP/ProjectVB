// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "VBFinisherCameraAuthoringSession.generated.h"

class AActor;
class UVBFinisherConfig;
class IAssetEditorInstance;
struct FContextualAnimTrack;
struct FPropertyChangedEvent;

/**
 * 처형 설정 자산 하나의 카메라 저작 상태와 동작(에디터 전용). 데이터는 '무엇을 재생하나' 만, '어떻게 저작하나' 는 에디터 모듈이 맡는다.
 * 다시 그릴 때마다 버려지는 디테일 패널 대신 이 객체가 상태를 쥔다. 주인은 UVBFinisherCameraAuthoringSubsystem 이고 자산은 약참조로만 본다.
 */
UCLASS(Transient)
class UVBFinisherCameraAuthoringSession : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UVBFinisherConfig* InConfig);
	UVBFinisherConfig* GetConfig() const { return Config.Get(); }

	// 이 세션이 아직 쓸 수 있는가. 자산이 사라졌으면 false 다 - 그때 버튼은 아무것도 안 한다.
	bool IsAlive() const { return Config.IsValid(); }

	// 미리보기를 전부 거둔다(재생 정지 -> 카메라, 액터 제거 -> 배치 액터 복원 -> 델리게이트 해제). 멱등이고,
	//   자산이 먼저 사라져도 타이머, 임시 액터가 남지 않게 자산 없이도 돈다. 반환은 다시 보이게 한 배치 액터 수다.
	int32 ReleasePreview();

	// --- 버튼이 부르는 동작. 판단은 전부 여기 있고 버튼은 부르기만 한다 ---

	/**
	 * 지금 뷰포트 화면 그대로 키를 하나 만든다(카메라 자리, 볼 지점, FOV, 미리보기 시각) - 숫자 없이 구도를 잡는 저작의 정문이다.
	 * 같은 시각에 키가 있으면 덮어쓴다(길이 0 구간은 보간할 수 없다). 스스로 되돌리기 기록을 연다.
	 */
	void AddKeyFromViewport();

	/**
	 * 키를 레벨의 카메라 액터로 꺼낸다(키 하나에 카메라 하나) - 기즈모, Ctrl+Shift+P 로 붙잡고 옮기면 키에 바로 반영된다.
	 * 기준 액터(처형당할 쪽)를 먼저 선택해야 한다.
	 */
	void SpawnKeyCameras();

	// 꺼낸 카메라들을 지금 상태 그대로 키에 반영한다(위치, 보는 방향, FOV).
	//   옮기는 것은 자동 반영되므로, FOV 처럼 이동이 아닌 변경과 확인용이다. 스스로 되돌리기 기록을 연다.
	void SyncKeysFromCameras();

	// 「다룰 키」가 잡는 그림을 지금 뷰포트로 보여준다. 값 -> 화면.
	//   숫자만 보고 구도를 떠올릴 수는 없다.
	void PreviewKey();

	// 지금 뷰포트 구도를 「다룰 키」에 담는다. 화면 -> 값. FOV 까지 담는다. 스스로 되돌리기 기록을 연다.
	void CaptureKeyFromViewport();

	/**
	 * 선택한 액터 자리에 처형 자세의 미리보기 액터를 세운다(미리보기 시각을 끌면 다시 불린다). 상대역도 저작된 상대 위치로 세운다.
	 * 선택한 액터는 좌표 기준으로만 쓰고 손대지 않는다 - 배치 액터의 애니 모드를 바꾸면 PIE 가 그 상태를 복제한다.
	 */
	void PosePreviewActors();

	/**
	 * 미리보기를 재생하거나 멈춘다(끝에 닿으면 처음부터 계속 돈다). 전환 버튼이라 누르면 상태가 반드시 바뀐다.
	 */
	void TogglePlay();

	// 상대역을 저작된 자리로 되돌린다. 평소에는 자리를 처음 한 번만 잡는다 - 손으로 옮긴 것을 지키려고.
	void ReplacePartner();

	// 미리보기를 끈다 - 세운 액터, 꺼낸 카메라를 지우고 숨긴 배치 액터를 다시 보이게 한다. 사람이 누르는 문이다.
	void ClearPosePreview();

	// --- 패널에 보이는 칸. 디테일 커스터마이제이션이 처형 설정 자산의 카테고리에 끼워 넣는다 ---
	// 저장하지 않는다(클래스가 Transient). 저작 중에만 뜻이 있는 값이고, 자산에 남으면
	//   다음 사람이 「이게 게임에 쓰이나」를 한 번 의심하게 된다.

	// 저작 도구가 다룰 키 번호. 버튼은 자산 하나에 붙고 키는 배열이라 「어느 키에 대해」를 어딘가에서 말해야 한다.
	UPROPERTY(EditAnywhere, Category="VB|FinisherAuthoring",
		meta=(ClampMin="0", DisplayName="다룰 키 번호 (시각·선택을 따라온다)"))
	int32 EditorKeyIndex = 0;

	/**
	 * 미리보기 시각(초). 끌면 선택한 액터가 그 시각의 처형 자세를 잡는다 - 카메라 키의 시간축이 장면 재생 시간이라서다.
	 * 슬라이더를 끄는 동안 매번 갱신된다(PostEditChangeProperty 의 Interactive 변경).
	 */
	UPROPERTY(EditAnywhere, Category="VB|FinisherAuthoring",
		meta=(ClampMin="0.0", UIMin="0.0", UIMax="4.0", DisplayName="미리보기 시각(초)"))
	float EditorPreviewTime = 0.0f;

	// 지금 미리보기가 돌고 있는가(읽기 전용 표시). 이 값의 주인은 재생 타이머다.
	// NonTransactional - 되돌리기가 이 표시만 옛 값으로 돌리면 타이머와 패널이 어긋난다.
	UPROPERTY(VisibleAnywhere, NonTransactional, Category="VB|FinisherAuthoring", meta=(DisplayName="재생 중"))
	bool bEditorPlaying = false;

	// 켜면 뷰포트가 저작 중인 카메라를 따라간다. 값은 런타임과 같은 UVBFinisherConfig::EvaluateCameraAt 이 낸다.
	//   기본이 꺼짐인 이유: 「이 키에 담기」는 사람이 뷰포트를 몰아야 하는데, 켜 두면 매번 도로 끌어간다.
	UPROPERTY(EditAnywhere, Category="VB|FinisherAuthoring", meta=(DisplayName="카메라 따라가기"))
	bool bEditorFollowCamera = false;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginDestroy() override;

private:
	// 슬라이더를 끄는 동안 매 틱 불리므로 조용해야 한다. 버튼으로 부를 때만 말한다.
	void PosePreviewActorsInternal(bool bVerbose);

	// 상대역(또는 앵커 대역)을 만들거나 이미 만든 것을 돌려준다.
	// bPreferAnchorMesh: 앵커 역할을 세울 때 켠다. 그 자리에 선 것을 대신 보여주므로 그 액터의 메시가 먼저다.
	AActor* EnsurePreviewActor(const AActor& Anchor, const FContextualAnimTrack& OtherTrack,
		TWeakObjectPtr<AActor>& Slot, const TCHAR* Label, bool bPreferAnchorMesh);

	// 자산이 사라졌으면 미리보기를 거두고 false 를 준다. 모든 동작이 맨 앞에서 부른다.
	bool EnsureConfigOrRelease();

	bool PlayTick(float DeltaSeconds);
	void StopPlayTicker();

	void OnActorMoved(AActor* Moved);
	void DestroyKeyCameras();
	void InvalidateKeyCameras();
	void SyncKeyIndexToPreviewTime();
	void OnObjectSelected(UObject* Selected);
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	// 창 닫기, PIE, 맵 변경을 잡는다. 미리보기 자원(재생 타이머 포함)을 처음 만들기 전에 건다 -
	//   없으면 창을 닫아도 재생이 안 멈춘다.
	void EnsureLifecycleHooks();
	void ReleaseLifecycleHooks();
	void OnAssetClosed(UObject* Asset, IAssetEditorInstance* Instance);
	void OnLevelGone();

	// 지금 거둘 미리보기 자원이 하나라도 있는가. 액터뿐 아니라 재생 타이머도 센다.
	bool HasPreviewResources() const;

	UPROPERTY()
	TWeakObjectPtr<UVBFinisherConfig> Config;

	// 미리보기로 세운 상대역. 플레이어는 런타임에 소환되므로 레벨에 없다.
	TWeakObjectPtr<AActor> PreviewPartner;

	// 앵커 역할(대개 피해자)의 대역. 레벨에 놓인 액터는 좌표 기준으로만 쓰고 손대지 않는다.
	TWeakObjectPtr<AActor> PreviewSelf;

	// 미리보기 동안 잠깐 숨긴 배치 액터들(bHiddenEdTemporary - 저장되지 않고 PIE 에도 안 따라간다).
	TArray<TWeakObjectPtr<AActor>> HiddenActors;

	// 키를 꺼내 세운 카메라들. 배열 순서가 키 번호와 같다 - 중간이 비면 nullptr 이 들어간다.
	TArray<TWeakObjectPtr<AActor>> KeyCameras;

	// 키를 꺼낼 때의 기준 액터. 되돌려 쓸 때 선택에서 다시 찾으면 못 찾는다 - 그때 선택된 것은 카메라다.
	TWeakObjectPtr<AActor> KeyAnchor;

	// 다음 갱신에서 상대역 자리를 다시 잡을 것인가. 「상대역 자리 다시 잡기」가 켠다.
	bool bForcePlacePartner = false;

	// 키에 되돌려 쓰는 중인가. 우리가 쓴 것이 다시 우리를 부르는 것을 막는다.
	bool bSyncingKeys = false;

	FTSTicker::FDelegateHandle PlayTickerHandle;
	FDelegateHandle ActorMovedHandle;
	FDelegateHandle SelectionHandle;
	FDelegateHandle AssetClosedHandle;
	FDelegateHandle PieHandle;
	FDelegateHandle MapOpenedHandle;
	FDelegateHandle PropertyChangedHandle;
};
