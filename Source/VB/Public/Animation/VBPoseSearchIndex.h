// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPoseSearchDatabase;

/**
 * 포즈 검색 DB 색인을 쓰기 전에 준비한다. 에디터는 색인을 비동기로 만들어 그 사이 검색 결과가 0 이다.
 * 게임 빌드는 쿡된 색인을 쓰므로 두 함수 모두 할 일이 없다.
 */
namespace VBPoseSearchIndex
{
	// 비동기 빌드를 시작만 한다. 레벨 시작처럼 곧 쓸 것을 아는 자리에서 부른다.
	VB_API void Prewarm(TConstArrayView<const UPoseSearchDatabase*> Databases);

	// 검색할 DB 색인이 준비될 때까지 기다린다(에디터 전용). 빌드 실패한 DB 는 빼고 경고한다.
	VB_API void EnsureReady(TArray<UPoseSearchDatabase*>& InOutDatabases, const TCHAR* Purpose);
}
