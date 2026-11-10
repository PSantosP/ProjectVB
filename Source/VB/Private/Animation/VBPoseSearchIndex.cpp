// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBPoseSearchIndex.h"

#include "PoseSearch/PoseSearchDatabase.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "HAL/PlatformTime.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#endif

namespace VBPoseSearchIndex
{
	void Prewarm(TConstArrayView<const UPoseSearchDatabase*> Databases)
	{
#if WITH_EDITOR
		using namespace UE::PoseSearch;
		for (const UPoseSearchDatabase* Database : Databases)
		{
			if (Database)
			{
				FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest);
			}
		}
#endif
	}

	void EnsureReady(TArray<UPoseSearchDatabase*>& InOutDatabases, const TCHAR* Purpose)
	{
#if WITH_EDITOR
		using namespace UE::PoseSearch;
		const double Start = FPlatformTime::Seconds();
		bool bWaited = false;
		InOutDatabases.RemoveAll([Purpose, &bWaited](const UPoseSearchDatabase* Database)
		{
			if (!Database)
			{
				return true;
			}

			EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
				ERequestAsyncBuildFlag::ContinueRequest);
			if (Result == EAsyncBuildIndexResult::InProgress)
			{
				bWaited = true;
				Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
					ERequestAsyncBuildFlag::ContinueRequest | ERequestAsyncBuildFlag::WaitForCompletion);
			}

			if (Result != EAsyncBuildIndexResult::Success)
			{
				VB_LOG(Warning, "포즈 검색 색인 빌드 실패: %s (%s) - 이 DB 는 검색에서 뺀다. DB 자산의 경고를 보라", *GetNameSafe(Database), Purpose);
				return true;
			}

			return false;
		});
		if (bWaited)
		{
			VB_LOG(Log, "포즈 검색 색인이 아직 빌드 중이라 기다렸다: %s %.0fms (에디터 전용 - 미리 만들기가 덜 끝났다)",
				Purpose, (FPlatformTime::Seconds() - Start) * 1000.0);
		}
#endif
	}
}
