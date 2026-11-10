// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AGameStateBase;
class UVBStoryStateComponent;
class UWorld;

// 월드 표시물과 열린 화면이 함께 쓰는 범위 한정 이야기 구독.
// Refresh 에 전달한 Owner 의 멤버로 둔다 - 콜백은 그 Owner 의 weak lambda 이고 이 바인딩을 캡처한다.
class VB_API FVBStoryStateBinding
{
public:
	FVBStoryStateBinding() = default;
	~FVBStoryStateBinding();
	FVBStoryStateBinding(const FVBStoryStateBinding&) = delete;
	FVBStoryStateBinding& operator=(const FVBStoryStateBinding&) = delete;

	void Refresh(UObject* Owner, UWorld* World, FSimpleDelegate Requery);
	void Disconnect();
	UVBStoryStateComponent* GetStory() const;

private:
	void Rebind();
	void ExecuteRequery() const;
	TWeakObjectPtr<UObject> BoundOwner;
	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<UVBStoryStateComponent> Story;
	FSimpleDelegate RequestRequery;
	FDelegateHandle GameStateHandle;
	FDelegateHandle StoryHandle;
};
