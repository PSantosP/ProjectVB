#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
enum class EVisibilityBasedAnimTickOption : uint8;

/**
 * 화면 밖에서도 뼈를 갱신하라는 요청을 메시 하나에 대해 센다. 첫 요청 때 원래 값을 저장하고 마지막 해제 때 되돌린다.
 * 요청자가 둘(근접 판정 창, 겉모습 교체)이라 각자 저장, 복원하면 늦게 끝난 쪽이 덮어쓴다. 평소 값은 SetBasePolicy 로 쓴다.
 * 게임 스레드 전용. 메시가 사라지면 다음 호출에서 그 칸을 버린다.
 */
struct VB_API FVBMeshRefresh
{
	static void Acquire(USkeletalMeshComponent* Mesh);
	static void Release(USkeletalMeshComponent* Mesh);
	static int32 GetRequestCount(const USkeletalMeshComponent* Mesh);
	// 연결 층이 있는 애니메이션 클래스에 위험한 값이 오면 AlwaysTickPose 로 내리고 에러를 남긴다(IsBasePolicySafe).
	static void SetBasePolicy(USkeletalMeshComponent* Mesh, EVisibilityBasedAnimTickOption TickOption, bool bUpdateRateOptimizations);

	// 연결 노드(Linked Anim Graph/Layer)가 있는 클래스에 OnlyTickMontagesAndRefreshBonesWhenPlayingMontages 면 false - UE 5.8 크래시.
	// 인스턴스가 아니라 클래스로 본다(초기화 전 인스턴스 목록은 비어 있다). null, 네이티브 클래스는 true.
	static bool IsBasePolicySafe(EVisibilityBasedAnimTickOption TickOption, const UClass* AnimClass);
};
