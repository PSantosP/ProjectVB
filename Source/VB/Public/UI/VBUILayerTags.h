// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

// 화면 틀이 쓰는 층 이름. 게임 태그 목록과 분리해 UI 기반 코드가 게임 쪽을 몰라도 되게 한다.
namespace VBGameplayTags
{
	// UI.Layer - PrimaryLayout 의 stack 다섯, 앞의 넷은 Lyra 이름을 따른다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Game);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_GameMenu);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Menu);
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Modal);
	// Dialog 는 Modal 위에 뜨되 Options, KeyBindings 를 끄지 않는다
	VB_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_Dialog);
}
