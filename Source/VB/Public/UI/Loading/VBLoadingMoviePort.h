// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWidget;

/**
 * 로딩 화면 담당 클래스가 엔진 MoviePlayer 를 쓰는 유일한 접점. 맵을 여는 동안 로딩 스레드가 Slate 위젯을 그리게 한다.
 * 시험은 가짜로 바꿔 끼운다(헤드리스에서는 엔진 무비가 늘 꺼져 있다).
 */
class IVBLoadingMoviePort
{
public:
	virtual ~IVBLoadingMoviePort() = default;

	// 실제 플레이어(에디터, 데디케이티드 서버, commandlet, NullRHI, -NoLoadingScreen 아님)와 멀티 코어일 때만 쓸 수 있다.
	virtual bool IsAvailable() const = 0;
	virtual bool IsPlaying() const = 0;
	// 다른 설치자를 포함해 누군가 재생 속성을 준비해 두었는가.
	virtual bool IsPrepared() const = 0;
	// 맵 전용 재생을 건다. 엔진이 다음 PreLoadMap 콜백에서 시작한다.
	virtual void Install(const TSharedRef<SWidget>& Widget) = 0;
	// 재생 시작 전에만 지운다. 엔진 정리는 준비된 속성과 진행 중 재생이 둘 다 있어야 돈다.
	virtual void Clear() = 0;
	// 시작, 끝을 구독한다. 다시 묶으면 콜백을 바꾸고, Unbind 뒤로는 전달하지 않는다.
	virtual void Bind(TFunction<void()> OnStarted, TFunction<void()> OnFinished) = 0;
	virtual void Unbind() = 0;
};

namespace VBLoadingMovie
{
	// 엔진 MoviePlayer 의 실제 어댑터.
	VB_API TUniquePtr<IVBLoadingMoviePort> MakeEnginePort();
}
