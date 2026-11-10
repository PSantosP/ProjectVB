// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitFlashConfig.h"

#include "Materials/MaterialInterface.h"

const FVBHitFlashProfile& UVBHitFlashConfig::ResolveProfile(const FGameplayTag& CueTag) const
{
	// 정확 일치, 없으면 부모 태그를 위로 순회한다(자식 큐가 부모 GCN 으로 가는 엔진 동작과 같은 모양).
	// OverlayMaterial 이 빈 엔트리는 미등록으로 취급한다.
	for (FGameplayTag Tag = CueTag; Tag.IsValid(); Tag = Tag.RequestDirectParent())
	{
		if (const FVBHitFlashProfile* Profile = TagOverrides.Find(Tag))
		{
			if (Profile->OverlayMaterial)
			{
				return *Profile;
			}
		}
	}

	// 최종 폴백. 여기 머티리얼마저 비면 컴포넌트가 아무것도 안 그린다(순수 코스메틱).
	return DefaultProfile;
}
