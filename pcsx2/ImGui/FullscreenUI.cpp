// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "BuildVersion.h"
#include "CDVD/CDVDcommon.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSTexture.h"
#include "Achievements.h"
#include "CDVD/CDVDdiscReader.h"
#include "GameList.h"
#include "Host.h"
#include "Host/AudioStream.h"
#include "INISettingsInterface.h"
#include "ImGui/FullscreenUI.h"
#include "ImGui/FullscreenUI_Internal.h"
#include "ImGui/ImGuiFullscreen.h"
#include "ImGui/ImGuiManager.h"
#include "ImGui/ImGuiOverlays.h"
#include "Input/InputManager.h"
#include "MTGS.h"
#include "Patch.h"
#include "SupportURLs.h"
#include "USB/USB.h"
#include "VMManager.h"
#include "ps2/BiosTools.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/SettingsInterface.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Threading.h"

#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Sio.h"

#include "IconsFontAwesome.h"
#include "IconsPromptFont.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "fmt/chrono.h"

//////////////////////////////////////////////////////////////////////////
// Utility
//////////////////////////////////////////////////////////////////////////

TinyString FullscreenUI::TimeToPrintableString(time_t t)
{
	struct tm lt = {};
#ifdef _MSC_VER
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif

	TinyString ret;
	std::strftime(ret.data(), ret.buffer_size(), "%c", &lt);
	ret.update_size();
	return ret;
}

void FullscreenUI::GetStandardSelectionFooterText(SmallStringBase& dest, bool back_instead_of_cancel)
{
	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const auto glyphs = GetGamepadGlyphs();
		ImGuiFullscreen::CreateFooterTextString(
			dest,
			std::array{
				std::make_pair(glyphs.dpad_ud, FSUI_VSTR("Change Selection")),
				std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
				std::make_pair(glyphs.cancel(circleOK), back_instead_of_cancel ? FSUI_VSTR("Back") : FSUI_VSTR("Cancel")),
			});
	}
	else
	{
		ImGuiFullscreen::CreateFooterTextString(
			dest, std::array{
					  std::make_pair(ICON_PF_ARROW_UP ICON_PF_ARROW_DOWN, FSUI_VSTR("Change Selection")),
					  std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
					  std::make_pair(ICON_PF_ESC, back_instead_of_cancel ? FSUI_VSTR("Back") : FSUI_VSTR("Cancel")),
				  });
	}
}

void FullscreenUI::SetStandardSelectionFooterText(bool back_instead_of_cancel)
{
	SmallString text;
	GetStandardSelectionFooterText(text, back_instead_of_cancel);
	ImGuiFullscreen::SetFullscreenFooterText(text);
}

void ImGuiFullscreen::GetChoiceDialogHelpText(SmallStringBase& dest)
{
	FullscreenUI::GetStandardSelectionFooterText(dest, false);
}

void ImGuiFullscreen::GetFileSelectorHelpText(SmallStringBase& dest)
{
	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const bool swapNorthWest = ImGuiManager::IsGamepadNorthWestSwapped();
		const auto glyphs = GetGamepadGlyphs();
		ImGuiFullscreen::CreateFooterTextString(
			dest, std::array{
					  std::make_pair(glyphs.dpad_ud, FSUI_VSTR("Change Selection")),
					  std::make_pair(swapNorthWest ? glyphs.west : glyphs.north, FSUI_VSTR("Parent Directory")),
					  std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
					  std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Cancel")),
				  });
	}
	else
	{
		ImGuiFullscreen::CreateFooterTextString(
			dest,
			std::array{
				std::make_pair(ICON_PF_ARROW_UP ICON_PF_ARROW_DOWN, FSUI_VSTR("Change Selection")),
				std::make_pair(ICON_PF_BACKSPACE, FSUI_VSTR("Parent Directory")),
				std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
				std::make_pair(ICON_PF_ESC, FSUI_VSTR("Cancel")),
			});
	}
}

void ImGuiFullscreen::GetInputDialogHelpText(SmallStringBase& dest)
{
	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const auto glyphs = GetGamepadGlyphs();
		CreateFooterTextString(dest, std::array{
										 std::make_pair(ICON_PF_KEYBOARD, FSUI_VSTR("Enter Value")),
										 std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
										 std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Cancel")),
									 });
	}
	else
	{
		CreateFooterTextString(dest, std::array{
										 std::make_pair(ICON_PF_KEYBOARD, FSUI_VSTR("Enter Value")),
										 std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
										 std::make_pair(ICON_PF_ESC, FSUI_VSTR("Cancel")),
									 });
	}
}

void FullscreenUI::ApplyLayoutSettings(const SettingsInterface* bsi)
{
	ImGuiIO& io = ImGui::GetIO();
#define GET_SETTINGS_VALUE(type, section, key, default) \
	(bsi ? bsi->Get##type##Value(section, key, default) : Host::GetBase##type##SettingValue(section, key, default))

	SmallString swap_mode = GET_SETTINGS_VALUE(SmallString, "UI", "SwapOKFullscreenUI", "auto");

	// Check Nintendo Setting
	SmallString sdl2_nintendo_mode = GET_SETTINGS_VALUE(SmallString, "UI", "SDL2NintendoLayout", "false");
	// Check glyph preference
	SmallString glyph_mode = GET_SETTINGS_VALUE(SmallString, "UI", "FullscreenUIGlyphStyle", "auto");

	const auto parse_glyph_layout = [](const SmallString& mode) -> InputLayout {
		if (mode == "xbox")
			return InputLayout::Xbox;
		if (mode == "playstation")
			return InputLayout::Playstation;
		if (mode == "nintendo")
			return InputLayout::Nintendo;
		return InputLayout::Unknown;
	};

	switch (parse_glyph_layout(glyph_mode))
	{
		case InputLayout::Xbox:
			InputManager::SetGamepadIconPreference(InputLayout::Xbox);
			break;
		case InputLayout::Playstation:
			InputManager::SetGamepadIconPreference(InputLayout::Playstation);
			break;
		case InputLayout::Nintendo:
			InputManager::SetGamepadIconPreference(InputLayout::Nintendo);
			break;
		default:
			InputManager::SetGamepadIconPreference(InputLayout::Unknown);
			break;
	}

	const InputLayout layout = ImGuiFullscreen::GetGamepadLayout();

	if ((sdl2_nintendo_mode == "true" || sdl2_nintendo_mode == "auto") && layout == InputLayout::Nintendo)
	{
		// Apply
		ImGuiManager::SwapGamepadNorthWest(true);

		// Check swap_mode if A/B should also be swapped
		if (swap_mode == "auto")
		{
			io.ConfigNavSwapGamepadButtons = true;
			return;
		}
	}
	else
		ImGuiManager::SwapGamepadNorthWest(false);

	if (swap_mode == "true")
		io.ConfigNavSwapGamepadButtons = true;
	else if (swap_mode == "false")
		io.ConfigNavSwapGamepadButtons = false;
	else if (swap_mode == "auto")
	{
		// Check gamepad
		if (layout == InputLayout::Nintendo)
		{
			io.ConfigNavSwapGamepadButtons = true;
			return;
		}

		// Check language
		if (Host::LocaleCircleConfirm())
		{
			io.ConfigNavSwapGamepadButtons = true;
			return;
		}

		// Check BIOS
		SmallString bios_selection = GET_SETTINGS_VALUE(SmallString, "Filenames", "BIOS", "");

		if (bios_selection != "")
		{
			u32 bios_version, bios_region;
			std::string bios_description, bios_zone;
			if (IsBIOS(Path::Combine(EmuFolders::Bios, bios_selection).c_str(), bios_version, bios_description, bios_region, bios_zone))
			{
				// Japan, Asia, China
				if (bios_region == 0 || bios_region == 4 || bios_region == 6)
				{
					io.ConfigNavSwapGamepadButtons = true;
					return;
				}
			}
		}

		// X is confirm
		io.ConfigNavSwapGamepadButtons = false;
		return;
	}
	// Invalid setting
	else
		io.ConfigNavSwapGamepadButtons = false;
#undef GET_SETTINGS_VALUE
}

void FullscreenUI::LocaleChanged()
{
	ApplyLayoutSettings();
}

void FullscreenUI::GamepadLayoutChanged()
{
	ApplyLayoutSettings();
}

void FullscreenUI::PreferEnglishGameListChanged()
{
	s_prefer_english_titles = Host::GetBaseBoolSettingValue("UI", "PreferEnglishGameList", false);
}

// When drawing an svg to a non-integer size, we get a padded texture.
// This function crops off this padding by setting the image UV for the draw.
// We currently only use integer sizes for images, but I wrote this before checking that.
void FullscreenUI::DrawSvgTexture(GSTexture* padded_texture, ImVec2 unpadded_size)
{
	if (padded_texture != GetPlaceholderTexture().get())
	{
		const ImVec2 padded_size(padded_texture->GetWidth(), padded_texture->GetHeight());
		const ImVec2 uv1 = unpadded_size / padded_size;
		ImGui::Image(reinterpret_cast<ImTextureID>(padded_texture->GetNativeHandle()), unpadded_size, ImVec2(0.0f, 0.0f), uv1);
	}
	else
	{
		// Placeholder is a png file and should be scaled by ImGui
		ImGui::Image(reinterpret_cast<ImTextureID>(padded_texture->GetNativeHandle()), unpadded_size);
	}
}

void FullscreenUI::DrawCachedSvgTexture(const std::string& path, ImVec2 size, SvgScaling mode)
{
	DrawSvgTexture(GetCachedSvgTexture(path, size, mode), size);
}

void FullscreenUI::DrawCachedSvgTextureAsync(const std::string& path, ImVec2 size, SvgScaling mode)
{
	DrawSvgTexture(GetCachedSvgTextureAsync(path, size, mode), size);
}

// p_unpadded_max should be equal to p_min + unpadded_size
void FullscreenUI::DrawListSvgTexture(ImDrawList* drawList, GSTexture* padded_texture, const ImVec2& p_min, const ImVec2& p_unpadded_max)
{
	const ImVec2 unpadded_size = p_unpadded_max - p_min;
	if (padded_texture != GetPlaceholderTexture().get())
	{
		const ImVec2 padded_size(padded_texture->GetWidth(), padded_texture->GetHeight());
		const ImVec2 uv1 = unpadded_size / padded_size;
		drawList->AddImage(reinterpret_cast<ImTextureID>(padded_texture->GetNativeHandle()), p_min, p_unpadded_max, ImVec2(0.0f, 0.0f), uv1);
	}
	else
	{
		// Placeholder is a png file and should be scaled by ImGui
		drawList->AddImage(reinterpret_cast<ImTextureID>(padded_texture->GetNativeHandle()), p_min, p_unpadded_max);
	}
}

//////////////////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////////////////

bool FullscreenUI::Initialize()
{
	if (s_initialized)
		return true;

	if (s_tried_to_initialize)
		return false;

	ImGuiFullscreen::SetTheme(Host::GetBaseStringSettingValue("UI", "FullscreenUITheme", "Dark"));
	ImGuiFullscreen::UpdateLayoutScale();
	ImGuiFullscreen::UpdateFontScale();
	ApplyLayoutSettings();
	PreferEnglishGameListChanged();

	if (!ImGuiFullscreen::Initialize("fullscreenui/placeholder.png") || !LoadResources())
	{
		DestroyResources();
		ImGuiFullscreen::Shutdown(true);
		s_tried_to_initialize = true;
		return false;
	}

	s_initialized = true;
	s_hotkey_list_cache = InputManager::GetHotkeyList();
	MTGS::SetRunIdle(true);

	LoadCustomBackground();

	if (VMManager::HasValidVM())
	{
		UpdateGameDetails(VMManager::GetDiscPath(), VMManager::GetDiscSerial(), VMManager::GetTitle(s_prefer_english_titles), VMManager::GetDiscCRC(),
			VMManager::GetCurrentCRC());
	}
	else
	{
		const bool open_main_window = s_current_main_window == MainWindowType::None;
		if (open_main_window)
			ReturnToMainWindow();
	}

	ForceKeyNavEnabled();
	return true;
}

bool FullscreenUI::IsInitialized()
{
	return s_initialized;
}

void FullscreenUI::ReloadSvgResources()
{
	LoadSvgResources();
}

bool FullscreenUI::HasActiveWindow()
{
	return s_initialized && (s_current_main_window != MainWindowType::None || AreAnyDialogsOpen());
}

bool FullscreenUI::AreAnyDialogsOpen()
{
	return (s_save_state_selector_open || s_about_window_open ||
			s_input_binding_type != InputBindingInfo::Type::Unknown || ImGuiFullscreen::IsChoiceDialogOpen() ||
			ImGuiFullscreen::IsFileSelectorOpen());
}

void FullscreenUI::CheckForConfigChanges(const Pcsx2Config& old_config)
{
	if (!IsInitialized())
		return;

	ImGuiFullscreen::SetTheme(Host::GetBaseStringSettingValue("UI", "FullscreenUITheme", "Dark"));

	MTGS::RunOnGSThread([]() {
		LoadCustomBackground();
	});

	// If achievements got disabled, we might have the menu open...
	// That means we're going to be reaching achievement state.
	if (old_config.Achievements.Enabled && !EmuConfig.Achievements.Enabled)
	{
		// So, wait just in case.
		MTGS::RunOnGSThread([]() {
			if (s_current_main_window == MainWindowType::Achievements || s_current_main_window == MainWindowType::Leaderboards)
			{
				ReturnToPreviousWindow();
			}
		});
		MTGS::WaitGS(false, false, false);
	}

	if (old_config.FullpathToBios() != EmuConfig.FullpathToBios())
	{
		MTGS::RunOnGSThread([]() {
			ApplyLayoutSettings();
		});
	}
}

void FullscreenUI::OnVMStarted()
{
	if (!IsInitialized())
		return;

	MTGS::RunOnGSThread([]() {
		if (!IsInitialized())
			return;

		s_current_main_window = MainWindowType::None;
		QueueResetFocus(FocusResetType::WindowChanged);
	});
}

void FullscreenUI::OnVMDestroyed()
{
	if (!IsInitialized())
		return;

	MTGS::RunOnGSThread([]() {
		if (!IsInitialized())
			return;

		s_pause_menu_was_open = false;
		s_was_paused_on_quick_menu_open = false;
		s_current_pause_submenu = PauseSubMenu::None;
		ReturnToMainWindow();
	});
}

void FullscreenUI::GameChanged(std::string path, std::string serial, std::string title, u32 disc_crc, u32 crc)
{
	if (!IsInitialized())
		return;

	MTGS::RunOnGSThread([path = std::move(path), serial = std::move(serial), title = std::move(title), disc_crc, crc]() {
		if (!IsInitialized())
			return;

		UpdateGameDetails(std::move(path), std::move(serial), std::move(title), disc_crc, crc);
	});
}

void FullscreenUI::UpdateGameDetails(std::string path, std::string serial, std::string title, u32 disc_crc, u32 crc)
{
	if (!serial.empty())
		s_current_game_subtitle = fmt::format("{} / {:08X}", serial, crc);
	else
		s_current_game_subtitle = {};

	s_current_game_title = std::move(title);
	s_current_disc_serial = std::move(serial);
	s_current_disc_path = std::move(path);
	s_current_disc_crc = disc_crc;
}

void FullscreenUI::PauseForMenuOpen(bool set_pause_menu_open)
{
	s_was_paused_on_quick_menu_open = (VMManager::GetState() == VMState::Paused);
	if (Host::GetBoolSettingValue("UI", "PauseOnMenu", true) && !s_was_paused_on_quick_menu_open)
		Host::RunOnCPUThread([]() { VMManager::SetPaused(true); });

	s_pause_menu_was_open |= set_pause_menu_open;
}

void FullscreenUI::OpenPauseMenu()
{
	if (!VMManager::HasValidVM())
		return;

	if (SaveStateSelectorUI::IsOpen())
	{
		SaveStateSelectorUI::Close();
		return;
	}

	MTGS::RunOnGSThread([]() {
		if (!ImGuiManager::InitializeFullscreenUI() || s_current_main_window != MainWindowType::None)
			return;

		PauseForMenuOpen(true);
		ForceKeyNavEnabled();
		s_current_main_window = MainWindowType::PauseMenu;
		s_current_pause_submenu = PauseSubMenu::None;
		QueueResetFocus(FocusResetType::WindowChanged);
	});
}

void FullscreenUI::ClosePauseMenu()
{
	if (!IsInitialized() || !VMManager::HasValidVM())
		return;

	if (VMManager::GetState() == VMState::Paused && !s_was_paused_on_quick_menu_open)
		Host::RunOnCPUThread([]() { VMManager::SetPaused(false); });

	s_current_main_window = MainWindowType::None;
	s_current_pause_submenu = PauseSubMenu::None;
	s_pause_menu_was_open = false;
	QueueResetFocus(FocusResetType::WindowChanged);
}

void FullscreenUI::OpenPauseSubMenu(PauseSubMenu submenu)
{
	s_current_main_window = MainWindowType::PauseMenu;
	s_current_pause_submenu = submenu;
	QueueResetFocus(FocusResetType::WindowChanged);
}

void FullscreenUI::Shutdown(bool clear_state)
{
	if (clear_state)
	{
		CancelAllHddOperations();
		CloseSaveStateSelector();
		s_cover_image_map.clear();
		s_game_list_sorted_entries = {};
		s_game_list_directories_cache = {};
		s_game_cheat_unlabelled_count = 0;
		s_enabled_game_cheat_cache = {};
		s_game_cheats_list = {};
		s_enabled_game_patch_cache = {};
		s_game_patch_list = {};
		s_graphics_adapter_list_cache = {};
		s_current_game_title = {};
		s_current_game_subtitle = {};
		s_current_disc_serial = {};
		s_current_disc_path = {};
		s_current_disc_crc = 0;

		s_current_main_window = MainWindowType::None;
		s_current_pause_submenu = PauseSubMenu::None;
		s_pause_menu_was_open = false;
		s_was_paused_on_quick_menu_open = false;
		s_about_window_open = false;
	}
	s_hotkey_list_cache = {};

	s_custom_background_texture.reset();
	s_custom_background_path.clear();
	s_custom_background_enabled = false;

	DestroyResources();
	ImGuiFullscreen::Shutdown(clear_state);
	s_initialized = false;
	s_tried_to_initialize = false;
}

void FullscreenUI::Render()
{
	if (!s_initialized)
		return;

	// see if background setting changed
	static std::string s_last_background_path;
	std::string current_path = Host::GetBaseStringSettingValue("UI", "FSUIBackgroundPath");
	if (s_last_background_path != current_path)
	{
		s_last_background_path = current_path;
		LoadCustomBackground();
	}

	for (std::unique_ptr<GSTexture>& tex : s_cleanup_textures)
		g_gs_device->Recycle(tex.release());
	s_cleanup_textures.clear();
	ImGuiFullscreen::UploadAsyncTextures();

	ImGuiFullscreen::BeginLayout();

	const bool should_draw_background = (s_current_main_window == MainWindowType::Landing ||
		s_current_main_window == MainWindowType::StartGame ||
		s_current_main_window == MainWindowType::Exit ||
		s_current_main_window == MainWindowType::GameList ||
		s_current_main_window == MainWindowType::GameListSettings ||
		s_current_main_window == MainWindowType::Settings) &&
			!VMManager::HasValidVM() && s_custom_background_enabled && s_custom_background_texture;

	ImVec4 original_background_color;
	if (should_draw_background)
	{
		original_background_color = ImGuiFullscreen::UIBackgroundColor;
		DrawCustomBackground();
	}

	// Primed achievements must come first, because we don't want the pause screen to be behind them.
	if (s_current_main_window == MainWindowType::None && (EmuConfig.Achievements.Overlays || EmuConfig.Achievements.LBOverlays))
		Achievements::DrawGameOverlays();

	switch (s_current_main_window)
	{
		case MainWindowType::Landing:
			DrawLandingWindow();
			break;
		case MainWindowType::StartGame:
			DrawStartGameWindow();
			break;
		case MainWindowType::Exit:
			DrawExitWindow();
			break;
		case MainWindowType::GameList:
			DrawGameListWindow();
			break;
		case MainWindowType::GameListSettings:
			DrawGameListSettingsWindow();
			break;
		case MainWindowType::Settings:
			DrawSettingsWindow();
			break;
		case MainWindowType::PauseMenu:
			DrawPauseMenu(s_current_main_window);
			break;
		case MainWindowType::Achievements:
			Achievements::DrawAchievementsWindow();
			break;
		case MainWindowType::Leaderboards:
			Achievements::DrawLeaderboardsWindow();
			break;
		default:
			break;
	}

	if (s_save_state_selector_open)
	{
		if (s_save_state_selector_resuming)
			DrawResumeStateSelector();
		else
			DrawSaveStateSelector(s_save_state_selector_loading);
	}

	if (s_about_window_open)
		DrawAboutWindow();

	if (s_achievements_login_open || ImGui::IsPopupOpen("RetroAchievements"))
		DrawAchievementsLoginWindow();

	if (s_input_binding_type != InputBindingInfo::Type::Unknown)
		DrawInputBindingWindow();

	ImGuiFullscreen::EndLayout();

	if (s_settings_changed.load(std::memory_order_relaxed))
	{
		Host::CommitBaseSettingChanges();
		Host::RunOnCPUThread([]() { VMManager::ApplySettings(); });
		s_settings_changed.store(false, std::memory_order_release);
	}
	if (s_game_settings_changed.load(std::memory_order_relaxed))
	{
		if (s_game_settings_interface)
		{
			Error error;
			s_game_settings_interface->RemoveEmptySections();

			if (s_game_settings_interface->IsEmpty())
			{
				if (FileSystem::FileExists(s_game_settings_interface->GetFileName().c_str()) &&
					!FileSystem::DeleteFilePath(s_game_settings_interface->GetFileName().c_str(), &error))
				{
					ImGuiFullscreen::OpenInfoMessageDialog(
						FSUI_STR("Error"), fmt::format(FSUI_FSTR("An error occurred while deleting empty game settings:\n{}"),
											   error.GetDescription()));
				}
			}
			else
			{
				if (!s_game_settings_interface->Save(&error))
				{
					ImGuiFullscreen::OpenInfoMessageDialog(
						FSUI_STR("Error"),
						fmt::format(FSUI_FSTR("An error occurred while saving game settings:\n{}"), error.GetDescription()));
				}
			}

			if (VMManager::HasValidVM())
				Host::RunOnCPUThread([]() { VMManager::ReloadGameSettings(); });
		}
		s_game_settings_changed.store(false, std::memory_order_release);
	}

	if (should_draw_background)
		ImGuiFullscreen::UIBackgroundColor = original_background_color;

	ImGuiFullscreen::ResetCloseMenuIfNeeded();
}

void FullscreenUI::InvalidateCoverCache()
{
	if (!IsInitialized())
		return;

	MTGS::RunOnGSThread([]() { s_cover_image_map.clear(); });
}

void FullscreenUI::ReturnToPreviousWindow()
{
	if (VMManager::HasValidVM() && s_pause_menu_was_open)
	{
		s_current_main_window = MainWindowType::PauseMenu;
		QueueResetFocus(FocusResetType::WindowChanged);
	}
	else
	{
		ReturnToMainWindow();
	}
}

void FullscreenUI::ReturnToMainWindow()
{
	ClosePauseMenu();

	if (VMManager::HasValidVM())
	{
		s_current_main_window = MainWindowType::None;
		return;
	}

	if (ShouldDefaultToGameList())
		SwitchToGameList();
	else
		SwitchToLanding();
}

bool FullscreenUI::LoadResources()
{
	return LoadSvgResources();
}

bool FullscreenUI::LoadSvgResources()
{
	s_banner_texture = LoadSvgTexture("icons/AppBanner.svg", LayoutScale(500.0f, 76.0f), SvgScaling::Fit);

	for (u32 i = static_cast<u32>(GameDatabaseSchema::Compatibility::Nothing);
		 i <= static_cast<u32>(GameDatabaseSchema::Compatibility::Perfect); i++)
	{
		s_game_compatibility_textures[i - 1] = LoadSvgTexture(fmt::format("icons/star-{}.svg", i - 1).c_str(), LayoutScale(64.0f, 16.0f), SvgScaling::Fit);
	}

	return true;
}

void FullscreenUI::DestroyResources()
{
	s_banner_texture.reset();
	for (auto& tex : s_game_compatibility_textures)
		tex.reset();
	for (auto& tex : s_cleanup_textures)
		g_gs_device->Recycle(tex.release());
}

//////////////////////////////////////////////////////////////////////////
// Utility
//////////////////////////////////////////////////////////////////////////

ImGuiFullscreen::FileSelectorFilters FullscreenUI::GetOpenFileFilters()
{
	return {"*.bin", "*.iso", "*.cue", "*.mdf", "*.chd", "*.cso", "*.zso", "*.gz", "*.elf", "*.irx", "*.gs", "*.gs.xz", "*.gs.zst", "*.dump"};
}

ImGuiFullscreen::FileSelectorFilters FullscreenUI::GetDiscImageFilters()
{
	return {"*.bin", "*.iso", "*.cue", "*.mdf", "*.chd", "*.cso", "*.zso", "*.gz"};
}

ImGuiFullscreen::FileSelectorFilters FullscreenUI::GetAudioFileFilters()
{
	return {"*.wav"};
}

ImGuiFullscreen::FileSelectorFilters FullscreenUI::GetImageFileFilters()
{
	return {"*.png", "*.jpg", "*.jpeg", "*.bmp"};
}

void FullscreenUI::DoVMInitialize(const VMBootParameters& boot_params, bool switch_to_landing_on_failure)
{
	auto hardcore_disable_callback = [switch_to_landing_on_failure](
											   std::string reason, VMBootRestartCallback restart_callback) {
		MTGS::RunOnGSThread([reason = std::move(reason),
								restart_callback = std::move(restart_callback),
								switch_to_landing_on_failure]() {
			const auto callback = [restart_callback = std::move(restart_callback),
									  switch_to_landing_on_failure](bool confirmed) {
				if (confirmed)
					Host::RunOnCPUThread(restart_callback);
				else if (switch_to_landing_on_failure)
					SwitchToLanding();
			};

			ImGuiFullscreen::OpenConfirmMessageDialog(
				Achievements::GetHardcoreModeDisableTitle(),
				Achievements::GetHardcoreModeDisableText(reason.c_str()),
				std::move(callback), true,
				fmt::format(ICON_FA_CHECK " {}", TRANSLATE_SV("Achievements", "Yes")),
				fmt::format(ICON_FA_XMARK " {}", TRANSLATE_SV("Achievements", "No")));
		});
	};

	auto done_callback = [switch_to_landing_on_failure](VMBootResult result, const Error& error) {
		if (result != VMBootResult::StartupSuccess)
		{
			ImGuiFullscreen::OpenInfoMessageDialog(
				FSUI_ICONSTR(ICON_FA_TRIANGLE_EXCLAMATION, "Startup Error"), error.GetDescription());

			if (switch_to_landing_on_failure)
				MTGS::RunOnGSThread(SwitchToLanding);

			return;
		}

		VMManager::SetState(VMState::Running);
	};

	VMManager::InitializeAsync(boot_params, std::move(hardcore_disable_callback), std::move(done_callback));
}

void FullscreenUI::DoStartPath(const std::string& path, std::optional<s32> state_index, std::optional<bool> fast_boot)
{
	VMBootParameters params;
	params.filename = path;
	params.state_index = state_index;
	params.fast_boot = fast_boot;

	// switch to nothing, we'll get brought back if init fails
	Host::RunOnCPUThread([params = std::move(params)]() {
		DoVMInitialize(std::move(params), false);
	});
}

void FullscreenUI::DoStartFile()
{
	auto callback = [](const std::string& path) {
		if (!path.empty())
			DoStartPath(path);

		CloseFileSelector();
	};

	OpenFileSelector(FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Select Disc Image"), false, std::move(callback), GetOpenFileFilters());
}

void FullscreenUI::DoStartBIOS()
{
	Host::RunOnCPUThread([]() {
		if (VMManager::HasValidVM())
			return;

		VMBootParameters params;
		DoVMInitialize(std::move(params), true);
	});

	// switch to nothing, we'll get brought back if init fails
	s_current_main_window = MainWindowType::None;
}

void FullscreenUI::DoStartDisc(const std::string& drive)
{
	Host::RunOnCPUThread([drive]() {
		if (VMManager::HasValidVM())
			return;

		VMBootParameters params;
		params.filename = std::move(drive);
		params.source_type = CDVD_SourceType::Disc;
		DoVMInitialize(std::move(params), true);
	});
}

void FullscreenUI::DoStartDisc()
{
	std::vector<std::string> devices(GetOpticalDriveList());
	if (devices.empty())
	{
		ShowToast(std::string(), FSUI_STR("Could not find any CD/DVD-ROM devices. Please ensure you have a drive connected and sufficient "
										  "permissions to access it."));
		return;
	}

	// if there's only one, select it automatically
	if (devices.size() == 1)
	{
		DoStartDisc(devices.front());
		return;
	}

	ImGuiFullscreen::ChoiceDialogOptions options;
	for (std::string& drive : devices)
		options.emplace_back(std::move(drive), false);
	OpenChoiceDialog(
		FSUI_ICONSTR(ICON_FA_COMPACT_DISC, "Select Disc Drive"), false, std::move(options), [](s32, const std::string& path, bool) {
			DoStartDisc(path);
			CloseChoiceDialog();
		});
}

void FullscreenUI::DoToggleFrameLimit()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::SetLimiterMode(
			(VMManager::GetLimiterMode() != LimiterModeType::Unlimited) ? LimiterModeType::Unlimited : LimiterModeType::Nominal);
	});
}

void FullscreenUI::DoToggleSoftwareRenderer()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		MTGS::ToggleSoftwareRendering();
	});
}

void FullscreenUI::RequestShutdown(bool save_state)
{
	ConfirmShutdownIfMemcardBusy([save_state](bool result) {
		if (result)
			DoShutdown(save_state);

		ClosePauseMenu();
	});
}

void FullscreenUI::DoShutdown(bool save_state)
{
	Host::RunOnCPUThread([save_state]() { Host::RequestVMShutdown(false, save_state, save_state); });
}

void FullscreenUI::RequestReset()
{
	ConfirmShutdownIfMemcardBusy([](bool result) {
		if (result)
			DoReset();

		ClosePauseMenu();
	});
}

void FullscreenUI::DoReset()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::Reset();
	});
}

void FullscreenUI::DoChangeDiscFromFile()
{
	auto callback = [](const std::string& path) {
		if (!path.empty())
		{
			if (!VMManager::IsDiscFileName(path))
			{
				ShowToast({}, fmt::format(FSUI_FSTR("{} is not a valid disc image."), Path::GetFileName(path)));
			}
			else
			{
				Host::RunOnCPUThread([path]() { VMManager::ChangeDisc(CDVD_SourceType::Iso, std::move(path)); });
			}
		}

		CloseFileSelector();
		ReturnToPreviousWindow();
		ClosePauseMenu();
	};

	OpenFileSelector(FSUI_ICONSTR(ICON_FA_COMPACT_DISC, "Select Disc Image"), false, std::move(callback), GetDiscImageFilters(),
		std::string(Path::GetDirectory(s_current_disc_path)));
}

void FullscreenUI::RequestChangeDisc()
{
	ConfirmShutdownIfMemcardBusy([](bool result) {
		if (result)
			DoChangeDiscFromFile();
		else
			ClosePauseMenu();
	});
}

void FullscreenUI::DoRequestExit()
{
	Host::RunOnCPUThread([]() { Host::RequestExitApplication(true); });
}

void FullscreenUI::DoDesktopMode()
{
	Host::RunOnCPUThread([]() { Host::RequestExitBigPicture(); });
}

void FullscreenUI::DoToggleFullscreen()
{
	Host::RunOnCPUThread([]() { Host::SetFullscreen(!Host::IsFullscreen()); });
}

void FullscreenUI::ConfirmShutdownIfMemcardBusy(std::function<void(bool)> callback)
{
	if (!MemcardBusy::IsBusy())
	{
		callback(true);
		return;
	}

	OpenConfirmMessageDialog(FSUI_ICONSTR(ICON_PF_MEMORY_CARD, "WARNING: Memory Card Busy"),
		FSUI_STR("Your memory card is still saving data.\n\n"
			"WARNING: Shutting down now can IRREVERSIBLY CORRUPT YOUR MEMORY CARD.\n\n"
			"You are strongly advised to select 'No' and let the save finish.\n\n"
			"Do you want to shutdown anyway and IRREVERSIBLY CORRUPT YOUR MEMORY CARD?"),
		std::move(callback), false);
}

bool FullscreenUI::ShouldDefaultToGameList()
{
	return Host::GetBaseBoolSettingValue("UI", "FullscreenUIDefaultToGameList", false);
}

//////////////////////////////////////////////////////////////////////////
// Custom Background
//////////////////////////////////////////////////////////////////////////

void FullscreenUI::LoadCustomBackground()
{
	std::string path = Host::GetBaseStringSettingValue("UI", "FSUIBackgroundPath");

	if (path.empty())
	{
		s_custom_background_texture.reset();
		s_custom_background_path.clear();
		s_custom_background_enabled = false;
		return;
	}

	if (s_custom_background_path == path && s_custom_background_texture)
	{
		s_custom_background_enabled = true;
		return;
	}

	if (!Path::IsAbsolute(path))
		path = Path::Combine(EmuFolders::DataRoot, path);

	if (!FileSystem::FileExists(path.c_str()))
	{
		Console.Warning("Custom background file not found: %s", path.c_str());
		s_custom_background_texture.reset();
		s_custom_background_path.clear();
		s_custom_background_enabled = false;
		return;
	}

	if (StringUtil::EndsWithNoCase(path, ".gif"))
	{
		Console.Warning("GIF files aren't supported as backgrounds: %s", path.c_str());
		s_custom_background_texture.reset();
		s_custom_background_path.clear();
		s_custom_background_enabled = false;
		return;
	}

	if (StringUtil::EndsWithNoCase(path, ".webp"))
	{
		Console.Warning("WebP files aren't supported as backgrounds: %s", path.c_str());
		s_custom_background_texture.reset();
		s_custom_background_path.clear();
		s_custom_background_enabled = false;
		return;
	}

	s_custom_background_texture = LoadTexture(path.c_str());
	if (s_custom_background_texture)
	{
		s_custom_background_path = std::move(path);
		s_custom_background_enabled = true;
	}
	else
	{
		Console.Error("Failed to load custom background: %s", path.c_str());
		s_custom_background_path.clear();
		s_custom_background_enabled = false;
	}
}

void FullscreenUI::DrawCustomBackground()
{
	if (!s_custom_background_enabled || !s_custom_background_texture)
		return;

	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 display_size = io.DisplaySize;

	const u8 alpha = static_cast<u8>(Host::GetBaseFloatSettingValue("UI", "FSUIBackgroundOpacity", 100.0f) * 2.55f);
	const std::string mode = Host::GetBaseStringSettingValue("UI", "FSUIBackgroundMode", "fit");

	const float tex_width = static_cast<float>(s_custom_background_texture->GetWidth());
	const float tex_height = static_cast<float>(s_custom_background_texture->GetHeight());

	// Override the UIBackgroundColor that windows use
	// We need to make windows transparent so our background image shows through
	const ImVec4 transparent_bg = ImVec4(UIBackgroundColor.x, UIBackgroundColor.y, UIBackgroundColor.z, 0.0f);
	ImGuiFullscreen::UIBackgroundColor = transparent_bg;

	ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
	const ImU32 col = IM_COL32(255, 255, 255, alpha);
	const ImTextureID tex_id = reinterpret_cast<ImTextureID>(s_custom_background_texture->GetNativeHandle());

	if (mode == "stretch")
	{
		// stretch to fill entire display (ignores aspect ratio)
		bg_draw_list->AddImage(tex_id, ImVec2(0.0f, 0.0f), display_size, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), col);
	}
	else if (mode == "fill")
	{
		// Fill display while preserving aspect ratio (could crop edges)
		const float display_aspect = display_size.x / display_size.y;
		const float tex_aspect = tex_width / tex_height;

		float scale;
		if (tex_aspect > display_aspect)
		{
			// Image is wider scale to height and crop sides
			scale = display_size.y / tex_height;
		}
		else
		{
			// Image is taller scale to width and crop top/bottom
			scale = display_size.x / tex_width;
		}

		const float scaled_width = tex_width * scale;
		const float scaled_height = tex_height * scale;
		const float offset_x = (display_size.x - scaled_width) * 0.5f;
		const float offset_y = (display_size.y - scaled_height) * 0.5f;

		bg_draw_list->AddImage(tex_id,
			ImVec2(offset_x, offset_y),
			ImVec2(offset_x + scaled_width, offset_y + scaled_height),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), col);
	}
	else if (mode == "center")
	{
		// Center image at original size
		const float offset_x = (display_size.x - tex_width) * 0.5f;
		const float offset_y = (display_size.y - tex_height) * 0.5f;

		bg_draw_list->AddImage(tex_id,
			ImVec2(offset_x, offset_y),
			ImVec2(offset_x + tex_width, offset_y + tex_height),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), col);
	}
	else if (mode == "tile")
	{
		// Tile image across entire display
		// If the image is extremely small, this approach can generate millions of quads
		// and overflow the backend stream buffer (e.g. Vulkan assertion in VKStreamBuffer).
		// Since we cannot switch ImGui's sampler to wrap (yet), clamp the maximum number of quads
		constexpr int MAX_TILE_QUADS = 16384;

		float tile_width = tex_width;
		float tile_height = tex_height;
		int tiles_x = static_cast<int>(std::ceil(display_size.x / tile_width));
		int tiles_y = static_cast<int>(std::ceil(display_size.y / tile_height));

		const int total_tiles = tiles_x * tiles_y;
		if (total_tiles > MAX_TILE_QUADS)
		{
			const float scale = std::sqrt(static_cast<float>(total_tiles) / static_cast<float>(MAX_TILE_QUADS));
			tile_width *= scale;
			tile_height *= scale;
			tiles_x = static_cast<int>(std::ceil(display_size.x / tile_width));
			tiles_y = static_cast<int>(std::ceil(display_size.y / tile_height));
		}

		for (int y = 0; y < tiles_y; y++)
		{
			for (int x = 0; x < tiles_x; x++)
			{
				const float tile_x = static_cast<float>(x) * tile_width;
				const float tile_y = static_cast<float>(y) * tile_height;
				const float tile_max_x = std::min(tile_x + tile_width, display_size.x);
				const float tile_max_y = std::min(tile_y + tile_height, display_size.y);

				// get uvs for partial tiles at edges
				const float uv_max_x = (tile_max_x - tile_x) / tile_width;
				const float uv_max_y = (tile_max_y - tile_y) / tile_height;

				bg_draw_list->AddImage(tex_id,
					ImVec2(tile_x, tile_y),
					ImVec2(tile_max_x, tile_max_y),
					ImVec2(0.0f, 0.0f), ImVec2(uv_max_x, uv_max_y), col);
			}
		}
	}
	else // "fit" or default
	{
		// Fit on screen while preserving aspect ratio (no cropping)
		const float display_aspect = display_size.x / display_size.y;
		const float tex_aspect = tex_width / tex_height;

		float scale;
		if (tex_aspect > display_aspect)
		{
			// Image is wider than display
			scale = display_size.x / tex_width;
		}
		else
		{
			// Image is taller than display
			scale = display_size.y / tex_height;
		}

		const float scaled_width = tex_width * scale;
		const float scaled_height = tex_height * scale;
		const float offset_x = (display_size.x - scaled_width) * 0.5f;
		const float offset_y = (display_size.y - scaled_height) * 0.5f;

		bg_draw_list->AddImage(tex_id,
			ImVec2(offset_x, offset_y),
			ImVec2(offset_x + scaled_width, offset_y + scaled_height),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), col);
	}
}

//////////////////////////////////////////////////////////////////////////
// Landing Window
//////////////////////////////////////////////////////////////////////////

void FullscreenUI::SwitchToLanding()
{
	s_current_main_window = MainWindowType::Landing;
	QueueResetFocus(FocusResetType::WindowChanged);
}

void FullscreenUI::DrawLandingTemplate(ImVec2* menu_pos, ImVec2* menu_size)
{
	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 heading_size =
		ImVec2(io.DisplaySize.x, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY) +
									 (LayoutScale(LAYOUT_MENU_BUTTON_Y_PADDING) * 2.0f) + LayoutScale(2.0f));
	*menu_pos = ImVec2(0.0f, heading_size.y);
	*menu_size = ImVec2(io.DisplaySize.x, io.DisplaySize.y - heading_size.y - LayoutScale(LAYOUT_FOOTER_HEIGHT));

	if (BeginFullscreenWindow(ImVec2(0.0f, 0.0f), heading_size, "landing_heading", UIPrimaryColor))
	{
		const std::pair<ImFont*, float> heading_font = g_large_font;
		ImDrawList* const dl = ImGui::GetWindowDrawList();
		SmallString heading_str;

		ImGui::PushFont(heading_font.first, heading_font.second);
		ImGui::PushStyleColor(ImGuiCol_Text, UIPrimaryTextColor);

		// draw branding
		{
			const ImVec2 logo_pos = LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING, LAYOUT_MENU_BUTTON_Y_PADDING);
			const ImVec2 logo_size = LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
			dl->AddImage(reinterpret_cast<ImTextureID>(GetCachedTexture("icons/AppIconLarge.png")->GetNativeHandle()),
				logo_pos, logo_pos + logo_size);
			const ImVec2 branding_pos(logo_pos.x + logo_size.x + LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), logo_pos.y);
			ImGuiFullscreen::AddTextWithShadow(dl, heading_font, branding_pos, ImGui::GetColorU32(ImGuiCol_Text), "PCSX2");
		}

		// draw time
		ImVec2 time_pos;
		{
			// Waiting on (apple)clang support for P0355R7
			// Migrate to std::chrono::current_zone and zoned_time then
			const auto utc_now = std::chrono::system_clock::now();
			const auto utc_time_t = std::chrono::system_clock::to_time_t(utc_now);
			std::tm tm_local = {};
#ifdef _MSC_VER
			localtime_s(&tm_local, &utc_time_t);
#else
			localtime_r(&utc_time_t, &tm_local);
#endif
			heading_str.format(FSUI_FSTR("{:%H:%M}"), tm_local);

			const ImVec2 time_size = heading_font.first->CalcTextSizeA(heading_font.second, FLT_MAX, 0.0f, "00:00");
			time_pos = ImVec2(heading_size.x - LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING) - time_size.x,
				LayoutScale(LAYOUT_MENU_BUTTON_Y_PADDING));
			ImGuiFullscreen::AddTextWithShadow(
				dl, heading_font, time_pos, ImGui::GetColorU32(ImGuiCol_Text), heading_str.c_str(), heading_str.end_ptr());
			ImGui::RenderTextClipped(time_pos, time_pos + time_size, heading_str.c_str(), heading_str.end_ptr(), &time_size);
		}

		// draw achievements info
		if (Achievements::IsActive())
		{
			const auto lock = Achievements::GetLock();
			const char* username = Achievements::GetLoggedInUserName();
			if (username)
			{
				const ImVec2 name_size = heading_font.first->CalcTextSizeA(heading_font.second, FLT_MAX, 0.0f, username);
				const ImVec2 name_pos =
					ImVec2(time_pos.x - name_size.x - LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), time_pos.y);
				ImGuiFullscreen::AddTextWithShadow(dl, heading_font, name_pos, ImGui::GetColorU32(ImGuiCol_Text), username, nullptr);
				ImGui::RenderTextClipped(name_pos, name_pos + name_size, username, nullptr, &name_size);

				// TODO: should we cache this? heap allocations bad...
				std::string badge_path = Achievements::GetLoggedInUserBadgePath();
				if (!badge_path.empty()) [[likely]]
				{
					const ImVec2 badge_size =
						LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
					const ImVec2 badge_pos =
						ImVec2(name_pos.x - badge_size.x - LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING), time_pos.y);

					dl->AddImage(reinterpret_cast<ImTextureID>(GetCachedTextureAsync(badge_path)->GetNativeHandle()),
						badge_pos, badge_pos + badge_size);
				}
			}
		}

		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	EndFullscreenWindow();
}

void FullscreenUI::DrawLandingWindow()
{
	ImVec2 menu_pos, menu_size;
	DrawLandingTemplate(&menu_pos, &menu_size);

	ImGui::PushStyleColor(ImGuiCol_Text, UIBackgroundTextColor);

	if (BeginHorizontalMenu("landing_window", menu_pos, menu_size, 4))
	{
		ResetFocusHere();

		if (HorizontalMenuSvgItem("fullscreenui/media-cdrom.svg", FSUI_CSTR("Game List"),
				FSUI_CSTR("Launch a game from images scanned from your game directories.")))
		{
			SwitchToGameList();
		}

		if (HorizontalMenuSvgItem("fullscreenui/start-game.svg", FSUI_CSTR("Start Game"),
				FSUI_CSTR("Launch a game from a file, disc, or starts the console without any disc inserted.")))
		{
			s_current_main_window = MainWindowType::StartGame;
			QueueResetFocus(FocusResetType::WindowChanged);
		}

		if (HorizontalMenuSvgItem("fullscreenui/applications-system.svg", FSUI_CSTR("Settings"),
				FSUI_CSTR("Changes settings for the application.")))
		{
			SwitchToSettings();
		}

		if (HorizontalMenuSvgItem("fullscreenui/exit.svg", FSUI_CSTR("Exit"),
				FSUI_CSTR("Return to desktop mode, or exit the application.")) ||
			(!AreAnyDialogsOpen() && WantsToCloseMenu()))
		{
			s_current_main_window = MainWindowType::Exit;
			QueueResetFocus(FocusResetType::WindowChanged);
		}
	}
	ImGui::PopStyleColor();

	if (ImGui::Shortcut(ImGuiKey_GamepadBack) || ImGui::Shortcut(ImGuiKey_F1))
		OpenAboutWindow();
	if (ImGui::Shortcut(ImGuiKey_NavGamepadContextMenu) || ImGui::Shortcut(ImGuiKey_Space))
		SwitchToGameList();
	else if (ImGui::Shortcut(ImGuiKey_NavGamepadMenu) || ImGui::Shortcut(ImGuiKey_F11))
		DoToggleFullscreen();

	EndHorizontalMenu();

	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const bool swapNorthWest = ImGuiManager::IsGamepadNorthWestSwapped();
		const auto glyphs = GetGamepadGlyphs();
		SetFullscreenFooterText(std::array{
			std::make_pair(glyphs.select, FSUI_VSTR("About")),
			std::make_pair(glyphs.dpad_lr, FSUI_VSTR("Navigate")),
			std::make_pair(swapNorthWest ? glyphs.west : glyphs.north, FSUI_VSTR("Game List")),
			std::make_pair(swapNorthWest ? glyphs.north : glyphs.west, FSUI_VSTR("Toggle Fullscreen")),
			std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
			std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Exit")),
		});
	}
	else
	{
		SetFullscreenFooterText(std::array{
			std::make_pair(ICON_PF_F1, FSUI_VSTR("About")),
			std::make_pair(ICON_PF_F11, FSUI_VSTR("Toggle Fullscreen")),
			std::make_pair(ICON_PF_ARROW_LEFT ICON_PF_ARROW_RIGHT, FSUI_VSTR("Navigate")),
			std::make_pair(ICON_PF_SPACE, FSUI_VSTR("Game List")),
			std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
			std::make_pair(ICON_PF_ESC, FSUI_VSTR("Exit")),
		});
	}
}

void FullscreenUI::DrawStartGameWindow()
{
	ImVec2 menu_pos, menu_size;
	DrawLandingTemplate(&menu_pos, &menu_size);

	ImGui::PushStyleColor(ImGuiCol_Text, UIBackgroundTextColor);

	if (BeginHorizontalMenu("start_game_window", menu_pos, menu_size, 4))
	{
		ResetFocusHere();

		if (HorizontalMenuSvgItem("fullscreenui/start-file.svg", FSUI_CSTR("Start File"),
				FSUI_CSTR("Launch a game by selecting a file/disc image.")))
		{
			DoStartFile();
		}

		if (HorizontalMenuSvgItem("fullscreenui/drive-cdrom.svg", FSUI_CSTR("Start Disc"),
				FSUI_CSTR("Start a game from a disc in your PC's DVD drive.")))
		{
			DoStartDisc();
		}

		if (HorizontalMenuSvgItem("fullscreenui/start-bios.svg", FSUI_CSTR("Start BIOS"),
				FSUI_CSTR("Start the console without any disc inserted.")))
		{
			DoStartBIOS();
		}

		if (HorizontalMenuSvgItem("fullscreenui/back-icon.svg", FSUI_CSTR("Back"),
				FSUI_CSTR("Return to the previous menu.")) ||
			(!AreAnyDialogsOpen() && WantsToCloseMenu()))
		{
			s_current_main_window = MainWindowType::Landing;
			QueueResetFocus(FocusResetType::WindowChanged);
		}
	}

	ImGui::PopStyleColor();

	if (ImGui::Shortcut(ImGuiKey_NavGamepadMenu) || ImGui::Shortcut(ImGuiKey_F1))
		OpenSaveStateSelector(true);

	EndHorizontalMenu();

	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const bool swapNorthWest = ImGuiManager::IsGamepadNorthWestSwapped();
		const auto glyphs = GetGamepadGlyphs();
		SetFullscreenFooterText(std::array{
			std::make_pair(glyphs.dpad_lr, FSUI_VSTR("Navigate")),
			std::make_pair(swapNorthWest ? glyphs.north : glyphs.west, FSUI_VSTR("Load Global State")),
			std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
			std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Back")),
		});
	}
	else
	{
		SetFullscreenFooterText(std::array{
			std::make_pair(ICON_PF_ARROW_LEFT ICON_PF_ARROW_RIGHT, FSUI_VSTR("Navigate")),
			std::make_pair(ICON_PF_F1, FSUI_VSTR("Load Global State")),
			std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
			std::make_pair(ICON_PF_ESC, FSUI_VSTR("Back")),
		});
	}
}

void FullscreenUI::DrawExitWindow()
{
	ImVec2 menu_pos, menu_size;
	DrawLandingTemplate(&menu_pos, &menu_size);

	ImGui::PushStyleColor(ImGuiCol_Text, UIBackgroundTextColor);

	if (BeginHorizontalMenu("exit_window", menu_pos, menu_size, (Host::InNoGUIMode()) ? 2 : 3))
	{
		ResetFocusHere();

		if (HorizontalMenuSvgItem("fullscreenui/back-icon.svg", FSUI_CSTR("Back"),
				FSUI_CSTR("Return to the previous menu.")) ||
			WantsToCloseMenu())
		{
			s_current_main_window = MainWindowType::Landing;
			QueueResetFocus(FocusResetType::WindowChanged);
		}

		if (HorizontalMenuSvgItem("fullscreenui/exit.svg", FSUI_CSTR("Exit PCSX2"),
				FSUI_CSTR("Completely exits the application, returning you to your desktop.")))
		{
			DoRequestExit();
		}

		if (!Host::InNoGUIMode())
		{
			if (HorizontalMenuSvgItem("fullscreenui/desktop-mode.svg", FSUI_CSTR("Desktop Mode"),
					FSUI_CSTR("Exits Big Picture mode, returning to the desktop interface.")))
			{
				DoDesktopMode();
			}
		}
	}
	EndHorizontalMenu();

	ImGui::PopStyleColor();

	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const auto glyphs = GetGamepadGlyphs();
		SetFullscreenFooterText(std::array{
			std::make_pair(glyphs.dpad_lr, FSUI_VSTR("Navigate")),
			std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
			std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Back")),
		});
	}
	else
	{
<<<<<<< HEAD
		SetFullscreenFooterText(std::array{
			std::make_pair(ICON_PF_ARROW_LEFT ICON_PF_ARROW_RIGHT, FSUI_VSTR("Navigate")),
			std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
			std::make_pair(ICON_PF_ESC, FSUI_VSTR("Back")),
=======
		MenuButton(FSUI_ICONSTR(ICON_FA_BAN, "Cannot show details for games which were not scanned in the game list."), "");
	}

	MenuHeading(FSUI_CSTR("Options"));

	if (MenuButton(FSUI_ICONSTR(ICON_FA_COPY, "Copy Settings"), FSUI_CSTR("Copies the current global settings to this game.")))
		DoCopyGameSettings();
	if (MenuButton(FSUI_ICONSTR(ICON_FA_TRASH, "Clear Settings"), FSUI_CSTR("Clears all settings set for this game.")))
		DoClearGameSettings();

	EndMenuButtons();
}

void FullscreenUI::DrawInterfaceSettingsPage()
{
	SettingsInterface* bsi = GetEditingSettingsInterface();

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("Behaviour"));

	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_MAGIC, "Inhibit Screensaver"),
		FSUI_CSTR("Prevents the screen saver from activating and the host from sleeping while emulation is running."), "EmuCore",
		"InhibitScreensaver", true);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_CHARGING_STATION, "Enable Discord Presence"),
		FSUI_CSTR("Shows the game you are currently playing as part of your profile on Discord."), "UI", "DiscordPresence", false);

	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_CHARGING_STATION, "Enable MameHooker"),
		FSUI_CSTR("Enable MameHooker Outputs (need .NET 8)."), "UI", "EnableMameHooker", false);

	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_CHARGING_STATION, "AutoBoot SaveState 10"),
		FSUI_CSTR("If SaveState 10 exist, autoboot on it"), "UI", "AutoBootSaveStateTen", false);

	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_PAUSE, "Pause On Start"), FSUI_CSTR("Pauses the emulator when a game is started."), "UI",
		"StartPaused", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_VIDEO, "Pause On Focus Loss"),
		FSUI_CSTR("Pauses the emulator when you minimize the window or switch to another application, and unpauses when you switch back."),
		"UI", "PauseOnFocusLoss", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_WINDOW_MAXIMIZE, "Pause On Menu"),
		FSUI_CSTR("Pauses the emulator when you open the quick menu, and unpauses when you close it."), "UI", "PauseOnMenu", true);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_POWER_OFF, "Confirm Shutdown"),
		FSUI_CSTR("Determines whether a prompt will be displayed to confirm shutting down the emulator/game when the hotkey is pressed."),
		"UI", "ConfirmShutdown", true);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_SAVE, "Save State On Shutdown"),
		FSUI_CSTR("Automatically saves the emulator state when powering down or exiting. You can then resume directly from where you left "
				  "off next time."),
		"EmuCore", "SaveStateOnShutdown", false);
	if (DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_PAINT_BRUSH, "Use Light Theme"),
			FSUI_CSTR("Uses a light coloured theme instead of the default dark theme."), "UI", "UseLightFullscreenUITheme", false))
	{
		ImGuiFullscreen::SetTheme(bsi->GetBoolValue("UI", "UseLightFullscreenUITheme", false));
	}

	MenuHeading(FSUI_CSTR("Game Display"));
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_TV, "Start Fullscreen"),
		FSUI_CSTR("Automatically switches to fullscreen mode when a game is started."), "UI", "StartFullscreen", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_MOUSE, "Double-Click Toggles Fullscreen"),
		FSUI_CSTR("Switches between full screen and windowed when the window is double-clicked."), "UI", "DoubleClickTogglesFullscreen",
		true);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_MOUSE_POINTER, "Hide Cursor In Fullscreen"),
		FSUI_CSTR("Hides the mouse pointer/cursor when the emulator is in fullscreen mode."), "UI", "HideMouseCursor", false);

	MenuHeading(FSUI_CSTR("On-Screen Display"));
	DrawIntSpinBoxSetting(bsi, FSUI_ICONSTR(ICON_FA_SEARCH, "OSD Scale"),
		FSUI_CSTR("Determines how large the on-screen messages and monitor are."), "EmuCore/GS", "OsdScale", 100, 25, 500, 1, FSUI_CSTR("%d%%"));
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_LIST, "Show Messages"),
		FSUI_CSTR(
			"Shows on-screen-display messages when events occur such as save states being created/loaded, screenshots being taken, etc."),
		"EmuCore/GS", "OsdShowMessages", true);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_CLOCK, "Show Speed"),
		FSUI_CSTR("Shows the current emulation speed of the system in the top-right corner of the display as a percentage."), "EmuCore/GS",
		"OsdShowSpeed", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_RULER, "Show FPS"),
		FSUI_CSTR(
			"Shows the number of video frames (or v-syncs) displayed per second by the system in the top-right corner of the display."),
		"EmuCore/GS", "OsdShowFPS", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_BATTERY_HALF, "Show CPU Usage"),
		FSUI_CSTR("Shows the CPU usage based on threads in the top-right corner of the display."), "EmuCore/GS", "OsdShowCPU", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_SPINNER, "Show GPU Usage"),
		FSUI_CSTR("Shows the host's GPU usage in the top-right corner of the display."), "EmuCore/GS", "OsdShowGPU", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_RULER_VERTICAL, "Show Resolution"),
		FSUI_CSTR("Shows the resolution of the game in the top-right corner of the display."), "EmuCore/GS",
		"OsdShowResolution", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_BARS, "Show GS Statistics"),
		FSUI_CSTR("Shows statistics about GS (primitives, draw calls) in the top-right corner of the display."), "EmuCore/GS",
		"OsdShowGSStats", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_PLAY, "Show Status Indicators"),
		FSUI_CSTR("Shows indicators when fast forwarding, pausing, and other abnormal states are active."), "EmuCore/GS",
		"OsdShowIndicators", true);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_SLIDERS_H, "Show Settings"),
		FSUI_CSTR("Shows the current configuration in the bottom-right corner of the display."), "EmuCore/GS", "OsdShowSettings", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_GAMEPAD, "Show Inputs"),
		FSUI_CSTR("Shows the current controller state of the system in the bottom-left corner of the display."), "EmuCore/GS",
		"OsdShowInputs", false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_RULER_HORIZONTAL, "Show Frame Times"),
		FSUI_CSTR("Shows a visual history of frame times in the upper-left corner of the display."), "EmuCore/GS", "OsdShowFrameTimes",
		false);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_EXCLAMATION_CIRCLE, "Warn About Unsafe Settings"),
		FSUI_CSTR("Displays warnings when settings are enabled which may break games."), "EmuCore", "WarnAboutUnsafeSettings", true);

	MenuHeading(FSUI_CSTR("Operations"));
	if (MenuButton(FSUI_ICONSTR(ICON_FA_DUMPSTER_FIRE, "Reset Settings"),
			FSUI_CSTR("Resets configuration to defaults (excluding controller settings)."), !IsEditingGameSettings(bsi)))
	{
		DoResetSettings();
	}

	EndMenuButtons();
}

void FullscreenUI::DrawBIOSSettingsPage()
{
	SettingsInterface* bsi = GetEditingSettingsInterface();

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("BIOS Configuration"));

	DrawFolderSetting(bsi, FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Change Search Directory"), "Folders", "Bios", EmuFolders::Bios);

	const std::string bios_selection(GetEditingSettingsInterface()->GetStringValue("Filenames", "BIOS", ""));
	if (MenuButtonWithValue(FSUI_ICONSTR(ICON_FA_MICROCHIP, "BIOS Selection"),
			FSUI_CSTR("Changes the BIOS image used to start future sessions."),
			bios_selection.empty() ? FSUI_CSTR("Automatic") : bios_selection.c_str()))
	{
		ImGuiFullscreen::ChoiceDialogOptions choices;
		choices.emplace_back(FSUI_STR("Automatic"), bios_selection.empty());

		std::vector<std::string> values;
		values.push_back("");

		FileSystem::FindResultsArray results;
		FileSystem::FindFiles(EmuFolders::Bios.c_str(), "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_HIDDEN_FILES, &results);
		for (const FILESYSTEM_FIND_DATA& fd : results)
		{
			u32 version, region;
			std::string description, zone;
			if (!IsBIOS(fd.FileName.c_str(), version, description, region, zone))
				continue;

			const std::string_view filename(Path::GetFileName(fd.FileName));
			choices.emplace_back(fmt::format("{} ({})", description, filename), bios_selection == filename);
			values.emplace_back(filename);
		}

		OpenChoiceDialog(FSUI_CSTR("BIOS Selection"), false, std::move(choices),
			[game_settings = IsEditingGameSettings(bsi), values = std::move(values)](s32 index, const std::string& title, bool checked) {
				if (index < 0)
					return;

				auto lock = Host::GetSettingsLock();
				SettingsInterface* bsi = GetEditingSettingsInterface(game_settings);
				bsi->SetStringValue("Filenames", "BIOS", values[index].c_str());
				SetSettingsChanged(bsi);
				CloseChoiceDialog();
			});
	}

	MenuHeading(FSUI_CSTR("Options and Patches"));
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_LIGHTBULB, "Fast Boot"), FSUI_CSTR("Skips the intro screen, and bypasses region checks."),
		"EmuCore", "EnableFastBoot", true);

	EndMenuButtons();
}

void FullscreenUI::DrawEmulationSettingsPage()
{
	static constexpr int DEFAULT_FRAME_LATENCY = 2;

	static constexpr const char* speed_entries[] = {
		FSUI_NSTR("2% [1 FPS (NTSC) / 1 FPS (PAL)]"),
		FSUI_NSTR("10% [6 FPS (NTSC) / 5 FPS (PAL)]"),
		FSUI_NSTR("25% [15 FPS (NTSC) / 12 FPS (PAL)]"),
		FSUI_NSTR("50% [30 FPS (NTSC) / 25 FPS (PAL)]"),
		FSUI_NSTR("75% [45 FPS (NTSC) / 37 FPS (PAL)]"),
		FSUI_NSTR("90% [54 FPS (NTSC) / 45 FPS (PAL)]"),
		FSUI_NSTR("100% [60 FPS (NTSC) / 50 FPS (PAL)]"),
		FSUI_NSTR("110% [66 FPS (NTSC) / 55 FPS (PAL)]"),
		FSUI_NSTR("120% [72 FPS (NTSC) / 60 FPS (PAL)]"),
		FSUI_NSTR("150% [90 FPS (NTSC) / 75 FPS (PAL)]"),
		FSUI_NSTR("175% [105 FPS (NTSC) / 87 FPS (PAL)]"),
		FSUI_NSTR("200% [120 FPS (NTSC) / 100 FPS (PAL)]"),
		FSUI_NSTR("300% [180 FPS (NTSC) / 150 FPS (PAL)]"),
		FSUI_NSTR("400% [240 FPS (NTSC) / 200 FPS (PAL)]"),
		FSUI_NSTR("500% [300 FPS (NTSC) / 250 FPS (PAL)]"),
		FSUI_NSTR("1000% [600 FPS (NTSC) / 500 FPS (PAL)]"),
	};
	static constexpr const float speed_values[] = {
		0.02f,
		0.10f,
		0.25f,
		0.50f,
		0.75f,
		0.90f,
		1.00f,
		1.10f,
		1.20f,
		1.50f,
		1.75f,
		2.00f,
		3.00f,
		4.00f,
		5.00f,
		10.00f,
	};
	static constexpr const char* ee_cycle_rate_settings[] = {
		FSUI_NSTR("50% Speed"),
		FSUI_NSTR("60% Speed"),
		FSUI_NSTR("75% Speed"),
		FSUI_NSTR("100% Speed (Default)"),
		FSUI_NSTR("130% Speed"),
		FSUI_NSTR("180% Speed"),
		FSUI_NSTR("300% Speed"),
	};
	static constexpr const char* ee_cycle_skip_settings[] = {
		FSUI_NSTR("Normal (Default)"),
		FSUI_NSTR("Mild Underclock"),
		FSUI_NSTR("Moderate Underclock"),
		FSUI_NSTR("Maximum Underclock"),
	};
	static constexpr const char* affinity_control_settings[] = {
		FSUI_NSTR("Disabled"),
		FSUI_NSTR("EE > VU > GS"),
		FSUI_NSTR("EE > GS > VU"),
		FSUI_NSTR("VU > EE > GS"),
		FSUI_NSTR("VU > GS > EE"),
		FSUI_NSTR("GS > EE > VU"),
		FSUI_NSTR("GS > VU > EE"),
	};
	static constexpr const char* queue_entries[] = {
		FSUI_NSTR("0 Frames (Hard Sync)"),
		FSUI_NSTR("1 Frame"),
		FSUI_NSTR("2 Frames"),
		FSUI_NSTR("3 Frames"),
	};

	SettingsInterface* bsi = GetEditingSettingsInterface();

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("Speed Control"));

	DrawFloatListSetting(bsi, FSUI_CSTR("Normal Speed"), FSUI_CSTR("Sets the speed when running without fast forwarding."), "Framerate",
		"NominalScalar", 1.00f, speed_entries, speed_values, std::size(speed_entries), true);
	DrawFloatListSetting(bsi, FSUI_CSTR("Fast Forward Speed"), FSUI_CSTR("Sets the speed when using the fast forward hotkey."), "Framerate",
		"TurboScalar", 2.00f, speed_entries, speed_values, std::size(speed_entries), true);
	DrawFloatListSetting(bsi, FSUI_CSTR("Slow Motion Speed"), FSUI_CSTR("Sets the speed when using the slow motion hotkey."), "Framerate",
		"SlomoScalar", 0.50f, speed_entries, speed_values, std::size(speed_entries), true);
	DrawToggleSetting(bsi, FSUI_CSTR("Enable Speed Limiter"), FSUI_CSTR("When disabled, the game will run as fast as possible."),
		"EmuCore/GS", "FrameLimitEnable", true);

	MenuHeading(FSUI_CSTR("System Settings"));

	DrawIntListSetting(bsi, FSUI_CSTR("EE Cycle Rate"), FSUI_CSTR("Underclocks or overclocks the emulated Emotion Engine CPU."),
		"EmuCore/Speedhacks", "EECycleRate", 0, ee_cycle_rate_settings, std::size(ee_cycle_rate_settings), true, -3);
	DrawIntListSetting(bsi, FSUI_CSTR("EE Cycle Skipping"),
		FSUI_CSTR("Makes the emulated Emotion Engine skip cycles. Helps a small subset of games like SOTC. Most of the time it's harmful to performance."), "EmuCore/Speedhacks", "EECycleSkip", 0,
		ee_cycle_skip_settings, std::size(ee_cycle_skip_settings), true);
	DrawIntListSetting(bsi, FSUI_CSTR("Affinity Control Mode"),
		FSUI_CSTR("Pins emulation threads to CPU cores to potentially improve performance/frame time variance."), "EmuCore/CPU",
		"AffinityControlMode", 0, affinity_control_settings, std::size(affinity_control_settings), true);
	DrawToggleSetting(bsi, FSUI_CSTR("Enable MTVU (Multi-Threaded VU1)"),
		FSUI_CSTR("Generally a speedup on CPUs with 4 or more cores. Safe for most games, but a few are incompatible and may hang."), "EmuCore/Speedhacks", "vuThread", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Enable Instant VU1"),
		FSUI_CSTR("Runs VU1 instantly. Provides a modest speed improvement in most games. Safe for most games, but a few games may exhibit graphical errors."),
		"EmuCore/Speedhacks", "vu1Instant", true);
	DrawToggleSetting(
		bsi, FSUI_CSTR("Enable Cheats"), FSUI_CSTR("Enables loading cheats from pnach files."), "EmuCore", "EnableCheats", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Enable Host Filesystem"),
		FSUI_CSTR("Enables access to files from the host: namespace in the virtual machine."), "EmuCore", "HostFs", false);

	if (IsEditingGameSettings(bsi))
	{
		DrawToggleSetting(bsi, FSUI_CSTR("Enable Fast CDVD"), FSUI_CSTR("Fast disc access, less loading times. Not recommended."),
			"EmuCore/Speedhacks", "fastCDVD", false);
	}

	MenuHeading(FSUI_CSTR("Frame Pacing/Latency Control"));

	bool optimal_frame_pacing = (bsi->GetIntValue("EmuCore/GS", "VsyncQueueSize", DEFAULT_FRAME_LATENCY) == 0);

	DrawIntListSetting(bsi, FSUI_CSTR("Maximum Frame Latency"), FSUI_CSTR("Sets the number of frames which can be queued."), "EmuCore/GS",
		"VsyncQueueSize", DEFAULT_FRAME_LATENCY, queue_entries, std::size(queue_entries), true, 0, !optimal_frame_pacing);

	if (ToggleButton(FSUI_CSTR("Optimal Frame Pacing"),
			FSUI_CSTR("Synchronize EE and GS threads after each frame. Lowest input latency, but increases system requirements."),
			&optimal_frame_pacing))
	{
		bsi->SetIntValue("EmuCore/GS", "VsyncQueueSize", optimal_frame_pacing ? 0 : DEFAULT_FRAME_LATENCY);
		SetSettingsChanged(bsi);
	}

	DrawToggleSetting(bsi, FSUI_CSTR("Scale To Host Refresh Rate"),
		FSUI_CSTR("Speeds up emulation so that the guest refresh rate matches the host."), "EmuCore/GS", "SyncToHostRefreshRate", false);

	EndMenuButtons();
}

void FullscreenUI::DrawClampingModeSetting(SettingsInterface* bsi, const char* title, const char* summary, int vunum)
{
	// This is so messy... maybe we should just make the mode an int in the settings too...
	const bool base = IsEditingGameSettings(bsi) ? 1 : 0;
	std::optional<bool> default_false = IsEditingGameSettings(bsi) ? std::nullopt : std::optional<bool>(false);
	std::optional<bool> default_true = IsEditingGameSettings(bsi) ? std::nullopt : std::optional<bool>(true);

	std::optional<bool> third = bsi->GetOptionalBoolValue(
		"EmuCore/CPU/Recompiler", (vunum >= 0 ? ((vunum == 0) ? "vu0SignOverflow" : "vu1SignOverflow") : "fpuFullMode"), default_false);
	std::optional<bool> second = bsi->GetOptionalBoolValue("EmuCore/CPU/Recompiler",
		(vunum >= 0 ? ((vunum == 0) ? "vu0ExtraOverflow" : "vu1ExtraOverflow") : "fpuExtraOverflow"), default_false);
	std::optional<bool> first = bsi->GetOptionalBoolValue(
		"EmuCore/CPU/Recompiler", (vunum >= 0 ? ((vunum == 0) ? "vu0Overflow" : "vu1Overflow") : "fpuOverflow"), default_true);

	int index;
	if (third.has_value() && third.value())
		index = base + 3;
	else if (second.has_value() && second.value())
		index = base + 2;
	else if (first.has_value() && first.value())
		index = base + 1;
	else if (first.has_value())
		index = base + 0; // none
	else
		index = 0; // no per game override

	static constexpr const char* ee_clamping_mode_settings[] = {
		FSUI_NSTR("Use Global Setting"),
		FSUI_NSTR("None"),
		FSUI_NSTR("Normal (Default)"),
		FSUI_NSTR("Extra + Preserve Sign"),
		FSUI_NSTR("Full"),
	};
	static constexpr const char* vu_clamping_mode_settings[] = {
		FSUI_NSTR("Use Global Setting"),
		FSUI_NSTR("None"),
		FSUI_NSTR("Normal (Default)"),
		FSUI_NSTR("Extra"),
		FSUI_NSTR("Extra + Preserve Sign"),
	};
	const char* const* options = (vunum >= 0) ? vu_clamping_mode_settings : ee_clamping_mode_settings;
	const int setting_offset = IsEditingGameSettings(bsi) ? 0 : 1;

	if (MenuButtonWithValue(title, summary, Host::TranslateToCString(TR_CONTEXT, options[index + setting_offset])))
	{
		ImGuiFullscreen::ChoiceDialogOptions cd_options;
		cd_options.reserve(std::size(ee_clamping_mode_settings));
		for (int i = setting_offset; i < static_cast<int>(std::size(ee_clamping_mode_settings)); i++)
			cd_options.emplace_back(Host::TranslateToString(TR_CONTEXT, options[i]), (i == (index + setting_offset)));
		OpenChoiceDialog(title, false, std::move(cd_options),
			[game_settings = IsEditingGameSettings(bsi), vunum](s32 index, const std::string& title, bool checked) {
				if (index >= 0)
				{
					auto lock = Host::GetSettingsLock();

					std::optional<bool> first, second, third;

					if (!game_settings || index > 0)
					{
						const bool base = game_settings ? 1 : 0;
						third = (index >= (base + 3));
						second = (index >= (base + 2));
						first = (index >= (base + 1));
					}

					SettingsInterface* bsi = GetEditingSettingsInterface(game_settings);
					bsi->SetOptionalBoolValue("EmuCore/CPU/Recompiler",
						(vunum >= 0 ? ((vunum == 0) ? "vu0SignOverflow" : "vu1SignOverflow") : "fpuFullMode"), third);
					bsi->SetOptionalBoolValue("EmuCore/CPU/Recompiler",
						(vunum >= 0 ? ((vunum == 0) ? "vu0ExtraOverflow" : "vu1ExtraOverflow") : "fpuExtraOverflow"), second);
					bsi->SetOptionalBoolValue(
						"EmuCore/CPU/Recompiler", (vunum >= 0 ? ((vunum == 0) ? "vu0Overflow" : "vu1Overflow") : "fpuOverflow"), first);
					SetSettingsChanged(bsi);
				}

				CloseChoiceDialog();
			});
	}
}

void FullscreenUI::DrawGraphicsSettingsPage()
{
	static constexpr const char* s_renderer_names[] = {
		FSUI_NSTR("Automatic (Default)"),
#ifdef _WIN32
		FSUI_NSTR("Direct3D 11"),
		FSUI_NSTR("Direct3D 12"),
#endif
#ifdef ENABLE_OPENGL
		FSUI_NSTR("OpenGL"),
#endif
#ifdef ENABLE_VULKAN
		FSUI_NSTR("Vulkan"),
#endif
#ifdef __APPLE__
		FSUI_NSTR("Metal"),
#endif
		FSUI_NSTR("Software"),
		FSUI_NSTR("Null"),
	};
	static constexpr const char* s_renderer_values[] = {
		"-1", //GSRendererType::Auto,
#ifdef _WIN32
		"3", //GSRendererType::DX11,
		"15", //GSRendererType::DX12,
#endif
#ifdef ENABLE_OPENGL
		"12", //GSRendererType::OGL,
#endif
#ifdef ENABLE_VULKAN
		"14", //GSRendererType::VK,
#endif
#ifdef __APPLE__
		"17", //GSRendererType::Metal,
#endif
		"13", //GSRendererType::SW,
		"11", //GSRendererType::Null
	};
	static constexpr const char* s_vsync_values[] = {
		FSUI_NSTR("Off"),
		FSUI_NSTR("On"),
		FSUI_NSTR("Adaptive"),
	};
	static constexpr const char* s_bilinear_present_options[] = {
		FSUI_NSTR("Off"),
		FSUI_NSTR("Bilinear (Smooth)"),
		FSUI_NSTR("Bilinear (Sharp)"),
	};
	static constexpr const char* s_deinterlacing_options[] = {
		FSUI_NSTR("Automatic (Default)"),
		FSUI_NSTR("No Deinterlacing"),
		FSUI_NSTR("Weave (Top Field First, Sawtooth)"),
		FSUI_NSTR("Weave (Bottom Field First, Sawtooth)"),
		FSUI_NSTR("Bob (Top Field First)"),
		FSUI_NSTR("Bob (Bottom Field First)"),
		FSUI_NSTR("Blend (Top Field First, Half FPS)"),
		FSUI_NSTR("Blend (Bottom Field First, Half FPS)"),
		FSUI_NSTR("Adaptive (Top Field First)"),
		FSUI_NSTR("Adaptive (Bottom Field First)"),
	};
	static const char* s_resolution_options[] = {
		FSUI_NSTR("Native (PS2)"),
		FSUI_NSTR("1.25x Native"),
		FSUI_NSTR("1.5x Native"),
		FSUI_NSTR("1.75x Native"),
		FSUI_NSTR("2x Native (~720p)"),
		FSUI_NSTR("2.25x Native"),
		FSUI_NSTR("2.5x Native"),
		FSUI_NSTR("2.75x Native"),
		FSUI_NSTR("3x Native (~1080p)"),
		FSUI_NSTR("3.5x Native"),
		FSUI_NSTR("4x Native (~1440p/2K)"),
		FSUI_NSTR("5x Native (~1620p)"),
		FSUI_NSTR("6x Native (~2160p/4K)"),
		FSUI_NSTR("7x Native (~2520p)"),
		FSUI_NSTR("8x Native (~2880p)"),
	};
	static const char* s_resolution_values[] = {
		"1",
		"1.25",
		"1.5",
		"1.75",
		"2",
		"2.25",
		"2.5",
		"2.75",
		"3",
		"3.5",
		"4",
		"5",
		"6",
		"7",
		"8",
	};
	static constexpr const char* s_mipmapping_options[] = {
		FSUI_NSTR("Automatic (Default)"),
		FSUI_NSTR("Off"),
		FSUI_NSTR("Basic (Generated Mipmaps)"),
		FSUI_NSTR("Full (PS2 Mipmaps)"),
	};
	static constexpr const char* s_bilinear_options[] = {
		FSUI_NSTR("Nearest"),
		FSUI_NSTR("Bilinear (Forced)"),
		FSUI_NSTR("Bilinear (PS2)"),
		FSUI_NSTR("Bilinear (Forced excluding sprite)"),
	};
	static constexpr const char* s_trilinear_options[] = {
		FSUI_NSTR("Automatic (Default)"),
		FSUI_NSTR("Off (None)"),
		FSUI_NSTR("Trilinear (PS2)"),
		FSUI_NSTR("Trilinear (Forced)"),
	};
	static constexpr const char* s_dithering_options[] = {
		FSUI_NSTR("Off"),
		FSUI_NSTR("Scaled"),
		FSUI_NSTR("Unscaled (Default)"),
	};
	static constexpr const char* s_blending_options[] = {
		FSUI_NSTR("Minimum"),
		FSUI_NSTR("Basic (Recommended)"),
		FSUI_NSTR("Medium"),
		FSUI_NSTR("High"),
		FSUI_NSTR("Full (Slow)"),
		FSUI_NSTR("Maximum (Very Slow)"),
	};
	static constexpr const char* s_anisotropic_filtering_entries[] = {
		FSUI_NSTR("Off (Default)"),
		FSUI_NSTR("2x"),
		FSUI_NSTR("4x"),
		FSUI_NSTR("8x"),
		FSUI_NSTR("16x"),
	};
	static constexpr const char* s_anisotropic_filtering_values[] = {"0", "2", "4", "8", "16"};
	static constexpr const char* s_preloading_options[] = {
		FSUI_NSTR("None"),
		FSUI_NSTR("Partial"),
		FSUI_NSTR("Full (Hash Cache)"),
	};
	static constexpr const char* s_generic_options[] = {
		FSUI_NSTR("Automatic (Default)"),
		FSUI_NSTR("Force Disabled"),
		FSUI_NSTR("Force Enabled"),
	};
	static constexpr const char* s_hw_download[] = {
		FSUI_NSTR("Accurate (Recommended)"),
		FSUI_NSTR("Disable Readbacks (Synchronize GS Thread)"),
		FSUI_NSTR("Unsynchronized (Non-Deterministic)"),
		FSUI_NSTR("Disabled (Ignore Transfers)"),
	};
	static constexpr const char* s_screenshot_sizes[] = {
		FSUI_NSTR("Screen Resolution"),
		FSUI_NSTR("Internal Resolution"),
		FSUI_NSTR("Internal Resolution (Aspect Uncorrected)"),
	};
	static constexpr const char* s_screenshot_formats[] = {
		FSUI_NSTR("PNG"),
		FSUI_NSTR("JPEG"),
	};

	SettingsInterface* bsi = GetEditingSettingsInterface();

	const GSRendererType renderer =
		static_cast<GSRendererType>(GetEffectiveIntSetting(bsi, "EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::Auto)));
	const bool is_hardware = (renderer == GSRendererType::Auto || renderer == GSRendererType::DX11 || renderer == GSRendererType::DX12 ||
							  renderer == GSRendererType::OGL || renderer == GSRendererType::VK || renderer == GSRendererType::Metal);
	//const bool is_software = (renderer == GSRendererType::SW);

#ifndef PCSX2_DEVBUILD
	const bool hw_fixes_visible = is_hardware && IsEditingGameSettings(bsi);
#else
	const bool hw_fixes_visible = is_hardware;
#endif

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("Renderer"));
	DrawStringListSetting(bsi, FSUI_CSTR("Renderer"), FSUI_CSTR("Selects the API used to render the emulated GS."), "EmuCore/GS",
		"Renderer", "-1", s_renderer_names, s_renderer_values, std::size(s_renderer_names), true);
	DrawIntListSetting(bsi, FSUI_CSTR("Sync To Host Refresh (VSync)"), FSUI_CSTR("Synchronizes frame presentation with host refresh."),
		"EmuCore/GS", "VsyncEnable", static_cast<int>(VsyncMode::Off), s_vsync_values, std::size(s_vsync_values), true);

	MenuHeading(FSUI_CSTR("Display"));
	DrawStringListSetting(bsi, FSUI_CSTR("Aspect Ratio"), FSUI_CSTR("Selects the aspect ratio to display the game content at."),
		"EmuCore/GS", "AspectRatio", "Auto 4:3/3:2", Pcsx2Config::GSOptions::AspectRatioNames, Pcsx2Config::GSOptions::AspectRatioNames, 0,
		false);
	DrawStringListSetting(bsi, FSUI_CSTR("FMV Aspect Ratio"),
		FSUI_CSTR("Selects the aspect ratio for display when a FMV is detected as playing."), "EmuCore/GS", "FMVAspectRatioSwitch",
		"Auto 4:3/3:2", Pcsx2Config::GSOptions::FMVAspectRatioSwitchNames, Pcsx2Config::GSOptions::FMVAspectRatioSwitchNames, 0, false);
	DrawIntListSetting(bsi, FSUI_CSTR("Deinterlacing"),
		FSUI_CSTR("Selects the algorithm used to convert the PS2's interlaced output to progressive for display."), "EmuCore/GS",
		"deinterlace_mode", static_cast<int>(GSInterlaceMode::Automatic), s_deinterlacing_options, std::size(s_deinterlacing_options),
		true);
	DrawIntListSetting(bsi, FSUI_CSTR("Screenshot Size"), FSUI_CSTR("Determines the resolution at which screenshots will be saved."),
		"EmuCore/GS", "ScreenshotSize", static_cast<int>(GSScreenshotSize::WindowResolution), s_screenshot_sizes,
		std::size(s_screenshot_sizes), true);
	DrawIntListSetting(bsi, FSUI_CSTR("Screenshot Format"), FSUI_CSTR("Selects the format which will be used to save screenshots."),
		"EmuCore/GS", "ScreenshotFormat", static_cast<int>(GSScreenshotFormat::PNG), s_screenshot_formats, std::size(s_screenshot_formats),
		true);
	DrawIntRangeSetting(bsi, FSUI_CSTR("Screenshot Quality"), FSUI_CSTR("Selects the quality at which screenshots will be compressed."),
		"EmuCore/GS", "ScreenshotQuality", 50, 1, 100, FSUI_CSTR("%d%%"));
	DrawIntRangeSetting(bsi, FSUI_CSTR("Vertical Stretch"), FSUI_CSTR("Increases or decreases the virtual picture size vertically."),
		"EmuCore/GS", "StretchY", 100, 10, 300, FSUI_CSTR("%d%%"));
	DrawIntRectSetting(bsi, FSUI_CSTR("Crop"), FSUI_CSTR("Crops the image, while respecting aspect ratio."), "EmuCore/GS", "CropLeft", 0,
		"CropTop", 0, "CropRight", 0, "CropBottom", 0, 0, 720, 1, FSUI_CSTR("%dpx"));

	if (!IsEditingGameSettings(bsi))
	{
		DrawToggleSetting(bsi, FSUI_CSTR("Enable Widescreen Patches"), FSUI_CSTR("Enables loading widescreen patches from pnach files."),
			"EmuCore", "EnableWideScreenPatches", false);
		DrawToggleSetting(bsi, FSUI_CSTR("Enable No-Interlacing Patches"),
			FSUI_CSTR("Enables loading no-interlacing patches from pnach files."), "EmuCore", "EnableNoInterlacingPatches", false);
	}

	DrawIntListSetting(bsi, FSUI_CSTR("Bilinear Upscaling"), FSUI_CSTR("Smooths out the image when upscaling the console to the screen."),
		"EmuCore/GS", "linear_present_mode", static_cast<int>(GSPostBilinearMode::BilinearSharp), s_bilinear_present_options,
		std::size(s_bilinear_present_options), true);
	DrawToggleSetting(bsi, FSUI_CSTR("Integer Upscaling"),
		FSUI_CSTR("Adds padding to the display area to ensure that the ratio between pixels on the host to pixels in the console is an "
				  "integer number. May result in a sharper image in some 2D games."),
		"EmuCore/GS", "IntegerScaling", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Screen Offsets"), FSUI_CSTR("Enables PCRTC Offsets which position the screen as the game requests."),
		"EmuCore/GS", "pcrtc_offsets", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Show Overscan"),
		FSUI_CSTR("Enables the option to show the overscan area on games which draw more than the safe area of the screen."), "EmuCore/GS",
		"pcrtc_overscan", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Anti-Blur"),
		FSUI_CSTR("Enables internal Anti-Blur hacks. Less accurate to PS2 rendering but will make a lot of games look less blurry."),
		"EmuCore/GS", "pcrtc_antiblur", true);

	MenuHeading(FSUI_CSTR("Rendering"));
	if (is_hardware)
	{
		DrawStringListSetting(bsi, FSUI_CSTR("Internal Resolution"),
			FSUI_CSTR("Multiplies the render resolution by the specified factor (upscaling)."), "EmuCore/GS", "upscale_multiplier",
			"1.000000", s_resolution_options, s_resolution_values, std::size(s_resolution_options), true);
		DrawIntListSetting(bsi, FSUI_CSTR("Mipmapping"), FSUI_CSTR("Determines how mipmaps are used when rendering textures."),
			"EmuCore/GS", "mipmap_hw", static_cast<int>(HWMipmapLevel::Automatic), s_mipmapping_options, std::size(s_mipmapping_options),
			true, -1);
		DrawIntListSetting(bsi, FSUI_CSTR("Bilinear Filtering"),
			FSUI_CSTR("Selects where bilinear filtering is utilized when rendering textures."), "EmuCore/GS", "filter",
			static_cast<int>(BiFiltering::PS2), s_bilinear_options, std::size(s_bilinear_options), true);
		DrawIntListSetting(bsi, FSUI_CSTR("Trilinear Filtering"),
			FSUI_CSTR("Selects where trilinear filtering is utilized when rendering textures."), "EmuCore/GS", "TriFilter",
			static_cast<int>(TriFiltering::Automatic), s_trilinear_options, std::size(s_trilinear_options), true, -1);
		DrawStringListSetting(bsi, FSUI_CSTR("Anisotropic Filtering"),
			FSUI_CSTR("Selects where anisotropic filtering is utilized when rendering textures."), "EmuCore/GS", "MaxAnisotropy", "0",
			s_anisotropic_filtering_entries, s_anisotropic_filtering_values, std::size(s_anisotropic_filtering_entries), true);
		DrawIntListSetting(bsi, FSUI_CSTR("Dithering"), FSUI_CSTR("Selects the type of dithering applies when the game requests it."),
			"EmuCore/GS", "dithering_ps2", 2, s_dithering_options, std::size(s_dithering_options), true);
		DrawIntListSetting(bsi, FSUI_CSTR("Blending Accuracy"),
			FSUI_CSTR("Determines the level of accuracy when emulating blend modes not supported by the host graphics API."), "EmuCore/GS",
			"accurate_blending_unit", static_cast<int>(AccBlendLevel::Basic), s_blending_options, std::size(s_blending_options), true);
		DrawIntListSetting(bsi, FSUI_CSTR("Texture Preloading"),
			FSUI_CSTR(
				"Uploads full textures to the GPU on use, rather than only the utilized regions. Can improve performance in some games."),
			"EmuCore/GS", "texture_preloading", static_cast<int>(TexturePreloadingLevel::Off), s_preloading_options,
			std::size(s_preloading_options), true);
	}
	else
	{
		DrawIntRangeSetting(bsi, FSUI_CSTR("Software Rendering Threads"),
			FSUI_CSTR("Number of threads to use in addition to the main GS thread for rasterization."), "EmuCore/GS", "extrathreads", 2, 0,
			10);
		DrawToggleSetting(bsi, FSUI_CSTR("Auto Flush (Software)"),
			FSUI_CSTR("Force a primitive flush when a framebuffer is also an input texture."), "EmuCore/GS", "autoflush_sw", true);
		DrawToggleSetting(bsi, FSUI_CSTR("Edge AA (AA1)"), FSUI_CSTR("Enables emulation of the GS's edge anti-aliasing (AA1)."),
			"EmuCore/GS", "aa1", true);
		DrawToggleSetting(
			bsi, FSUI_CSTR("Mipmapping"), FSUI_CSTR("Enables emulation of the GS's texture mipmapping."), "EmuCore/GS", "mipmap", true);
	}

	if (hw_fixes_visible)
	{
		MenuHeading(FSUI_CSTR("Hardware Fixes"));
		DrawToggleSetting(bsi, FSUI_CSTR("Manual Hardware Fixes"),
			FSUI_CSTR("Disables automatic hardware fixes, allowing you to set fixes manually."), "EmuCore/GS", "UserHacks", false);

		const bool manual_hw_fixes = GetEffectiveBoolSetting(bsi, "EmuCore/GS", "UserHacks", false);
		if (manual_hw_fixes)
		{
			static constexpr const char* s_cpu_sprite_render_bw_options[] = {
				FSUI_NSTR("0 (Disabled)"),
				FSUI_NSTR("1 (64 Max Width)"),
				FSUI_NSTR("2 (128 Max Width)"),
				FSUI_NSTR("3 (192 Max Width)"),
				FSUI_NSTR("4 (256 Max Width)"),
				FSUI_NSTR("5 (320 Max Width)"),
				FSUI_NSTR("6 (384 Max Width)"),
				FSUI_NSTR("7 (448 Max Width)"),
				FSUI_NSTR("8 (512 Max Width)"),
				FSUI_NSTR("9 (576 Max Width)"),
				FSUI_NSTR("10 (640 Max Width)"),
			};
			static constexpr const char* s_cpu_sprite_render_level_options[] = {
				FSUI_NSTR("Sprites Only"),
				FSUI_NSTR("Sprites/Triangles"),
				FSUI_NSTR("Blended Sprites/Triangles"),
			};
			static constexpr const char* s_cpu_clut_render_options[] = {
				FSUI_NSTR("0 (Disabled)"),
				FSUI_NSTR("1 (Normal)"),
				FSUI_NSTR("2 (Aggressive)"),
			};
			static constexpr const char* s_texture_inside_rt_options[] = {
				FSUI_NSTR("Disabled"),
				FSUI_NSTR("Inside Target"),
				FSUI_NSTR("Merge Targets"),
			};
			static constexpr const char* s_half_pixel_offset_options[] = {
				FSUI_NSTR("Off (Default)"),
				FSUI_NSTR("Normal (Vertex)"),
				FSUI_NSTR("Special (Texture)"),
				FSUI_NSTR("Special (Texture - Aggressive)"),
				FSUI_NSTR("Align To Native"),
			};
			static constexpr const char* s_round_sprite_options[] = {
				FSUI_NSTR("Off (Default)"),
				FSUI_NSTR("Half"),
				FSUI_NSTR("Full"),
			};
			static constexpr const char* s_bilinear_dirty_options[] = {
				FSUI_NSTR("Automatic (Default)"),
				FSUI_NSTR("Force Bilinear"),
				FSUI_NSTR("Force Nearest"),
			};
			static constexpr const char* s_auto_flush_options[] = {
				FSUI_NSTR("Disabled (Default)"),
				FSUI_NSTR("Enabled (Sprites Only)"),
				FSUI_NSTR("Enabled (All Primitives)"),
			};

			DrawIntListSetting(bsi, FSUI_CSTR("CPU Sprite Render Size"),
				FSUI_CSTR("Uses software renderer to draw texture decompression-like sprites."), "EmuCore/GS",
				"UserHacks_CPUSpriteRenderBW", 0, s_cpu_sprite_render_bw_options, std::size(s_cpu_sprite_render_bw_options), true);
			DrawIntListSetting(bsi, FSUI_CSTR("CPU Sprite Render Level"), FSUI_CSTR("Determines filter level for CPU sprite render."),
				"EmuCore/GS", "UserHacks_CPUSpriteRenderLevel", 0, s_cpu_sprite_render_level_options,
				std::size(s_cpu_sprite_render_level_options), true);
			DrawIntListSetting(bsi, FSUI_CSTR("Software CLUT Render"),
				FSUI_CSTR("Uses software renderer to draw texture CLUT points/sprites."), "EmuCore/GS", "UserHacks_CPUCLUTRender", 0,
				s_cpu_clut_render_options, std::size(s_cpu_clut_render_options), true);
			DrawIntSpinBoxSetting(bsi, FSUI_CSTR("Skip Draw Start"), FSUI_CSTR("Object range to skip drawing."), "EmuCore/GS",
				"UserHacks_SkipDraw_Start", 0, 0, 5000, 1);
			DrawIntSpinBoxSetting(bsi, FSUI_CSTR("Skip Draw End"), FSUI_CSTR("Object range to skip drawing."), "EmuCore/GS",
				"UserHacks_SkipDraw_End", 0, 0, 5000, 1);
			DrawIntListSetting(bsi, FSUI_CSTR("Auto Flush (Hardware)"),
				FSUI_CSTR("Force a primitive flush when a framebuffer is also an input texture."), "EmuCore/GS", "UserHacks_AutoFlushLevel",
				0, s_auto_flush_options, std::size(s_auto_flush_options), true, 0, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("CPU Framebuffer Conversion"),
				FSUI_CSTR("Convert 4-bit and 8-bit framebuffer on the CPU instead of the GPU."), "EmuCore/GS",
				"UserHacks_CPU_FB_Conversion", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Disable Depth Conversion"),
				FSUI_CSTR("Disable the support of depth buffers in the texture cache."), "EmuCore/GS", "UserHacks_DisableDepthSupport",
				false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Disable Safe Features"), FSUI_CSTR("This option disables multiple safe features."),
				"EmuCore/GS", "UserHacks_Disable_Safe_Features", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Disable Render Fixes"), FSUI_CSTR("This option disables game-specific render fixes."),
				"EmuCore/GS", "UserHacks_DisableRenderFixes", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Preload Frame Data"),
				FSUI_CSTR("Uploads GS data when rendering a new frame to reproduce some effects accurately."), "EmuCore/GS",
				"preload_frame_with_gs_data", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Disable Partial Invalidation"),
				FSUI_CSTR("Removes texture cache entries when there is any intersection, rather than only the intersected areas."),
				"EmuCore/GS", "UserHacks_DisablePartialInvalidation", false, manual_hw_fixes);
			DrawIntListSetting(bsi, FSUI_CSTR("Texture Inside RT"),
				FSUI_CSTR("Allows the texture cache to reuse as an input texture the inner portion of a previous framebuffer."),
				"EmuCore/GS", "UserHacks_TextureInsideRt", 0, s_texture_inside_rt_options, std::size(s_texture_inside_rt_options), true, 0,
				manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Read Targets When Closing"),
				FSUI_CSTR("Flushes all targets in the texture cache back to local memory when shutting down."), "EmuCore/GS",
				"UserHacks_ReadTCOnClose", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Estimate Texture Region"),
				FSUI_CSTR("Attempts to reduce the texture size when games do not set it themselves (e.g. Snowblind games)."), "EmuCore/GS",
				"UserHacks_EstimateTextureRegion", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("GPU Palette Conversion"),
				FSUI_CSTR("When enabled GPU converts colormap-textures, otherwise the CPU will. It is a trade-off between GPU and CPU."),
				"EmuCore/GS", "paltex", false);

			MenuHeading(FSUI_CSTR("Upscaling Fixes"));
			DrawIntListSetting(bsi, FSUI_CSTR("Half Pixel Offset"), FSUI_CSTR("Adjusts vertices relative to upscaling."), "EmuCore/GS",
				"UserHacks_HalfPixelOffset", 0, s_half_pixel_offset_options, std::size(s_half_pixel_offset_options), true);
			DrawIntListSetting(bsi, FSUI_CSTR("Round Sprite"), FSUI_CSTR("Adjusts sprite coordinates."), "EmuCore/GS",
				"UserHacks_round_sprite_offset", 0, s_round_sprite_options, std::size(s_round_sprite_options), true);
			DrawIntListSetting(bsi, FSUI_CSTR("Bilinear Upscale"),
				FSUI_CSTR("Can smooth out textures due to be bilinear filtered when upscaling. E.g. Brave sun glare."), "EmuCore/GS",
				"UserHacks_BilinearHack", static_cast<int>(GSBilinearDirtyMode::Automatic), s_bilinear_dirty_options,
				std::size(s_bilinear_dirty_options), true);
			DrawIntSpinBoxSetting(bsi, FSUI_CSTR("Texture Offset X"), FSUI_CSTR("Adjusts target texture offsets."), "EmuCore/GS",
				"UserHacks_TCOffsetX", 0, -4096, 4096, 1);
			DrawIntSpinBoxSetting(bsi, FSUI_CSTR("Texture Offset Y"), FSUI_CSTR("Adjusts target texture offsets."), "EmuCore/GS",
				"UserHacks_TCOffsetY", 0, -4096, 4096, 1);
			DrawToggleSetting(bsi, FSUI_CSTR("Align Sprite"), FSUI_CSTR("Fixes issues with upscaling (vertical lines) in some games."),
				"EmuCore/GS", "UserHacks_align_sprite_X", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Merge Sprite"),
				FSUI_CSTR("Replaces multiple post-processing sprites with a larger single sprite."), "EmuCore/GS",
				"UserHacks_merge_pp_sprite", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Wild Arms Hack"),
				FSUI_CSTR("Lowers the GS precision to avoid gaps between pixels when upscaling. Fixes the text on Wild Arms games."),
				"EmuCore/GS", "UserHacks_WildHack", false, manual_hw_fixes);
			DrawToggleSetting(bsi, FSUI_CSTR("Unscaled Palette Texture Draws"),
				FSUI_CSTR("Can fix some broken effects which rely on pixel perfect precision."), "EmuCore/GS",
				"UserHacks_NativePaletteDraw", false, manual_hw_fixes);
		}
	}

	if (is_hardware)
	{
		const bool dumping_active = GetEffectiveBoolSetting(bsi, "EmuCore/GS", "DumpReplaceableTextures", false);
		const bool replacement_active = GetEffectiveBoolSetting(bsi, "EmuCore/GS", "LoadTextureReplacements", false);

		MenuHeading(FSUI_CSTR("Texture Replacement"));
		DrawToggleSetting(bsi, FSUI_CSTR("Load Textures"), FSUI_CSTR("Loads replacement textures where available and user-provided."),
			"EmuCore/GS", "LoadTextureReplacements", false);
		DrawToggleSetting(bsi, FSUI_CSTR("Asynchronous Texture Loading"),
			FSUI_CSTR("Loads replacement textures on a worker thread, reducing microstutter when replacements are enabled."), "EmuCore/GS",
			"LoadTextureReplacementsAsync", true, replacement_active);
		DrawToggleSetting(bsi, FSUI_CSTR("Precache Replacements"),
			FSUI_CSTR("Preloads all replacement textures to memory. Not necessary with asynchronous loading."), "EmuCore/GS",
			"PrecacheTextureReplacements", false, replacement_active);
		DrawFolderSetting(bsi, FSUI_CSTR("Replacements Directory"), FSUI_CSTR("Folders"), "Textures", EmuFolders::Textures);

		MenuHeading(FSUI_CSTR("Texture Dumping"));
		DrawToggleSetting(bsi, FSUI_CSTR("Dump Textures"), FSUI_CSTR("Dumps replaceable textures to disk. Will reduce performance."),
			"EmuCore/GS", "DumpReplaceableTextures", false);
		DrawToggleSetting(bsi, FSUI_CSTR("Dump Mipmaps"), FSUI_CSTR("Includes mipmaps when dumping textures."), "EmuCore/GS",
			"DumpReplaceableMipmaps", false, dumping_active);
		DrawToggleSetting(bsi, FSUI_CSTR("Dump FMV Textures"),
			FSUI_CSTR("Allows texture dumping when FMVs are active. You should not enable this."), "EmuCore/GS",
			"DumpTexturesWithFMVActive", false, dumping_active);
	}

	MenuHeading(FSUI_CSTR("Post-Processing"));
	{
		static constexpr const char* s_cas_options[] = {
			FSUI_NSTR("None (Default)"),
			FSUI_NSTR("Sharpen Only (Internal Resolution)"),
			FSUI_NSTR("Sharpen and Resize (Display Resolution)"),
		};
		const bool cas_active = (GetEffectiveIntSetting(bsi, "EmuCore/GS", "CASMode", 0) != static_cast<int>(GSCASMode::Disabled));

		DrawToggleSetting(bsi, FSUI_CSTR("FXAA"), FSUI_CSTR("Enables FXAA post-processing shader."), "EmuCore/GS", "fxaa", false);
		DrawIntListSetting(bsi, FSUI_CSTR("Contrast Adaptive Sharpening"), FSUI_CSTR("Enables FidelityFX Contrast Adaptive Sharpening."),
			"EmuCore/GS", "CASMode", static_cast<int>(GSCASMode::Disabled), s_cas_options, std::size(s_cas_options), true);
		DrawIntSpinBoxSetting(bsi, FSUI_CSTR("CAS Sharpness"),
			FSUI_CSTR("Determines the intensity the sharpening effect in CAS post-processing."), "EmuCore/GS", "CASSharpness", 50, 0, 100,
			1, FSUI_CSTR("%d%%"), cas_active);
	}

	MenuHeading(FSUI_CSTR("Filters"));
	{
		const bool shadeboost_active = GetEffectiveBoolSetting(bsi, "EmuCore/GS", "ShadeBoost", false);

		DrawToggleSetting(bsi, FSUI_CSTR("Shade Boost"), FSUI_CSTR("Enables brightness/contrast/saturation adjustment."), "EmuCore/GS",
			"ShadeBoost", false);
		DrawIntRangeSetting(bsi, FSUI_CSTR("Shade Boost Brightness"), FSUI_CSTR("Adjusts brightness. 50 is normal."), "EmuCore/GS",
			"ShadeBoost_Brightness", 50, 1, 100, "%d", shadeboost_active);
		DrawIntRangeSetting(bsi, FSUI_CSTR("Shade Boost Contrast"), FSUI_CSTR("Adjusts contrast. 50 is normal."), "EmuCore/GS",
			"ShadeBoost_Contrast", 50, 1, 100, "%d", shadeboost_active);
		DrawIntRangeSetting(bsi, FSUI_CSTR("Shade Boost Saturation"), FSUI_CSTR("Adjusts saturation. 50 is normal."), "EmuCore/GS",
			"ShadeBoost_Saturation", 50, 1, 100, "%d", shadeboost_active);

		static constexpr const char* s_tv_shaders[] = {FSUI_NSTR("None (Default)"), FSUI_NSTR("Scanline Filter"),
			FSUI_NSTR("Diagonal Filter"), FSUI_NSTR("Triangular Filter"), FSUI_NSTR("Wave Filter"), FSUI_NSTR("Lottes CRT"),
			FSUI_NSTR("4xRGSS"), FSUI_NSTR("NxAGSS")};
		DrawIntListSetting(bsi, FSUI_CSTR("TV Shaders"), FSUI_CSTR("Applies a shader which replicates the visual effects of different styles of television set."), "EmuCore/GS", "TVShader", 0,
			s_tv_shaders, std::size(s_tv_shaders), true);
	}

	static constexpr const char* s_gsdump_compression[] = {FSUI_NSTR("Uncompressed"), FSUI_NSTR("LZMA (xz)"), FSUI_NSTR("Zstandard (zst)")};

	MenuHeading(FSUI_CSTR("Advanced"));
	DrawToggleSetting(bsi, FSUI_CSTR("Skip Presenting Duplicate Frames"),
		FSUI_CSTR("Skips displaying frames that don't change in 25/30fps games. Can improve speed, but increase input lag/make frame pacing "
				  "worse."),
		"EmuCore/GS", "SkipDuplicateFrames", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Disable Threaded Presentation"),
		FSUI_CSTR("Presents frames on the main GS thread instead of a worker thread. Used for debugging frametime issues."),
		"EmuCore/GS", "DisableThreadedPresentation", false);
	if (hw_fixes_visible)
	{
		DrawIntListSetting(bsi, FSUI_CSTR("Hardware Download Mode"), FSUI_CSTR("Changes synchronization behavior for GS downloads."),
			"EmuCore/GS", "HWDownloadMode", static_cast<int>(GSHardwareDownloadMode::Enabled), s_hw_download, std::size(s_hw_download),
			true);
	}
	DrawIntListSetting(bsi, FSUI_CSTR("Allow Exclusive Fullscreen"),
		FSUI_CSTR("Overrides the driver's heuristics for enabling exclusive fullscreen, or direct flip/scanout."), "EmuCore/GS",
		"ExclusiveFullscreenControl", -1, s_generic_options, std::size(s_generic_options), true, -1,
		(renderer == GSRendererType::Auto || renderer == GSRendererType::VK));
	DrawIntListSetting(bsi, FSUI_CSTR("Override Texture Barriers"),
		FSUI_CSTR("Forces texture barrier functionality to the specified value."), "EmuCore/GS", "OverrideTextureBarriers", -1,
		s_generic_options, std::size(s_generic_options), true, -1);
	DrawIntListSetting(bsi, FSUI_CSTR("GS Dump Compression"), FSUI_CSTR("Sets the compression algorithm for GS dumps."), "EmuCore/GS",
		"GSDumpCompression", static_cast<int>(GSDumpCompressionMethod::LZMA), s_gsdump_compression, std::size(s_gsdump_compression), true);
	DrawToggleSetting(bsi, FSUI_CSTR("Disable Framebuffer Fetch"),
		FSUI_CSTR("Prevents the usage of framebuffer fetch when supported by host GPU."), "EmuCore/GS", "DisableFramebufferFetch", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Disable Dual-Source Blending"),
		FSUI_CSTR("Prevents the usage of dual-source blending when supported by host GPU."), "EmuCore/GS", "DisableDualSourceBlend", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Disable Shader Cache"), FSUI_CSTR("Prevents the loading and saving of shaders/pipelines to disk."),
		"EmuCore/GS", "DisableShaderCache", false);
	DrawToggleSetting(bsi, FSUI_CSTR("Disable Vertex Shader Expand"), FSUI_CSTR("Falls back to the CPU for expanding sprites/lines."),
		"EmuCore/GS", "DisableVertexShaderExpand", false);

	EndMenuButtons();
}

void FullscreenUI::DrawAudioSettingsPage()
{
	static constexpr const char* synchronization_modes[] = {
		FSUI_NSTR("TimeStretch (Recommended)"),
		FSUI_NSTR("Async Mix (Breaks some games!)"),
		FSUI_NSTR("None (Audio can skip.)"),
	};
	static constexpr const char* expansion_modes[] = {
		FSUI_NSTR("Stereo (None, Default)"),
		FSUI_NSTR("Quadraphonic"),
		FSUI_NSTR("Surround 5.1"),
		FSUI_NSTR("Surround 7.1"),
	};
	static constexpr const char* output_entries[] = {
		FSUI_NSTR("No Sound (Emulate SPU2 only)"),
		FSUI_NSTR("Cubeb (Cross-platform)"),
#ifdef _WIN32
		FSUI_NSTR("XAudio2"),
#endif
	};
	static constexpr const char* output_values[] = {
		"nullout",
		"cubeb",
#ifdef _WIN32
		"xaudio2",
#endif
	};
	static constexpr const char* default_output_module = "cubeb";

	SettingsInterface* bsi = GetEditingSettingsInterface();

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("Runtime Settings"));
	DrawIntRangeSetting(bsi, FSUI_ICONSTR(ICON_FA_VOLUME_UP, "Output Volume"),
		FSUI_CSTR("Applies a global volume modifier to all sound produced by the game."), "SPU2/Mixing", "FinalVolume", 100, 0, 200,
		FSUI_CSTR("%d%%"));

	MenuHeading(FSUI_CSTR("Mixing Settings"));
	DrawIntListSetting(bsi, FSUI_ICONSTR(ICON_FA_RULER, "Synchronization Mode"),
		FSUI_CSTR("Changes when SPU samples are generated relative to system emulation."), "SPU2/Output", "SynchMode",
		static_cast<int>(Pcsx2Config::SPU2Options::SynchronizationMode::TimeStretch), synchronization_modes,
		std::size(synchronization_modes), true);
	DrawIntListSetting(bsi, FSUI_ICONSTR(ICON_FA_PLUS, "Expansion Mode"),
		FSUI_CSTR("Determines how the stereo output is transformed to greater speaker counts."), "SPU2/Output", "SpeakerConfiguration", 0,
		expansion_modes, std::size(expansion_modes), true);

	MenuHeading(FSUI_CSTR("Output Settings"));
	DrawStringListSetting(bsi, FSUI_ICONSTR(ICON_FA_PLAY_CIRCLE, "Output Module"),
		FSUI_CSTR("Determines which API is used to play back audio samples on the host."), "SPU2/Output", "OutputModule",
		default_output_module, output_entries, output_values, std::size(output_entries), true);
	DrawIntRangeSetting(bsi, FSUI_ICONSTR(ICON_FA_CLOCK, "Latency"),
		FSUI_CSTR("Sets the average output latency when using the cubeb backend."), "SPU2/Output", "Latency", 100, 15, 200, FSUI_CSTR("%d ms (avg)"));

	MenuHeading(FSUI_CSTR("Timestretch Settings"));
	DrawIntRangeSetting(bsi, FSUI_ICONSTR(ICON_FA_RULER_HORIZONTAL, "Sequence Length"),
		FSUI_CSTR("Affects how the timestretcher operates when not running at 100% speed."), "Soundtouch", "SequenceLengthMS", 30, 20, 100,
		FSUI_CSTR("%d ms"));
	DrawIntRangeSetting(bsi, FSUI_ICONSTR(ICON_FA_WINDOW_MAXIMIZE, "Seekwindow Size"),
		FSUI_CSTR("Affects how the timestretcher operates when not running at 100% speed."), "Soundtouch", "SeekWindowMS", 20, 10, 30,
		FSUI_CSTR("%d ms"));
	DrawIntRangeSetting(bsi, FSUI_ICONSTR(ICON_FA_RECEIPT, "Overlap"),
		FSUI_CSTR("Affects how the timestretcher operates when not running at 100% speed."), "Soundtouch", "OverlapMS", 20, 5, 15, FSUI_CSTR("%d ms"));

	EndMenuButtons();
}

void FullscreenUI::DrawMemoryCardSettingsPage()
{
	BeginMenuButtons();

	SettingsInterface* bsi = GetEditingSettingsInterface();

	MenuHeading(FSUI_CSTR("Settings and Operations"));
	if (MenuButton(FSUI_ICONSTR(ICON_FA_PLUS, "Create Memory Card"), FSUI_CSTR("Creates a new memory card file or folder.")))
		Host::OnCreateMemoryCardOpenRequested();

	DrawFolderSetting(bsi, FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Memory Card Directory"), "Folders", "MemoryCards", EmuFolders::MemoryCards);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_SEARCH, "Folder Memory Card Filter"),
		FSUI_CSTR("Simulates a larger memory card by filtering saves only to the current game."), "EmuCore", "McdFolderAutoManage", true);

	for (u32 port = 0; port < NUM_MEMORY_CARD_PORTS; port++)
	{
		SmallString str;
		str.fmt(FSUI_FSTR("Slot {}"), port + 1);
		MenuHeading(str.c_str());

		std::string enable_key(fmt::format("Slot{}_Enable", port + 1));
		std::string file_key(fmt::format("Slot{}_Filename", port + 1));

		DrawToggleSetting(bsi,
			SmallString::from_fmt(fmt::runtime(FSUI_ICONSTR_S(ICON_FA_SD_CARD, "Card Enabled", "##card_enabled_{}")), port),
			FSUI_CSTR("If not set, this card will be considered unplugged."), "MemoryCards", enable_key.c_str(), true);

		const bool enabled = GetEffectiveBoolSetting(bsi, "MemoryCards", enable_key.c_str(), true);

		std::optional<std::string> value(bsi->GetOptionalStringValue("MemoryCards", file_key.c_str(),
			IsEditingGameSettings(bsi) ? std::nullopt : std::optional<const char*>(FileMcd_GetDefaultName(port).c_str())));

		if (MenuButtonWithValue(SmallString::from_fmt(fmt::runtime(FSUI_ICONSTR_S(ICON_FA_FILE, "Card Name", "##card_name_{}")), port),
				FSUI_CSTR("The selected memory card image will be used for this slot."),
				value.has_value() ? value->c_str() : FSUI_CSTR("Use Global Setting"), enabled))
		{
			ImGuiFullscreen::ChoiceDialogOptions options;
			std::vector<std::string> names;
			if (IsEditingGameSettings(bsi))
				options.emplace_back(FSUI_STR("Use Global Setting"), !value.has_value());
			if (value.has_value() && !value->empty())
			{
				options.emplace_back(fmt::format(FSUI_FSTR("{} (Current)"), value.value()), true);
				names.push_back(std::move(value.value()));
			}
			for (AvailableMcdInfo& mci : FileMcd_GetAvailableCards(IsEditingGameSettings(bsi)))
			{
				if (mci.type == MemoryCardType::Folder)
				{
					options.emplace_back(fmt::format(FSUI_FSTR("{} (Folder)"), mci.name), false);
				}
				else
				{
					static constexpr const char* file_type_names[] = {
						FSUI_NSTR("Unknown"),
						FSUI_NSTR("PS2 (8MB)"),
						FSUI_NSTR("PS2 (16MB)"),
						FSUI_NSTR("PS2 (32MB)"),
						FSUI_NSTR("PS2 (64MB)"),
						FSUI_NSTR("PS1"),
					};
					options.emplace_back(fmt::format("{} ({})", mci.name,
											 Host::TranslateToStringView(TR_CONTEXT, file_type_names[static_cast<u32>(mci.file_type)])),
						false);
				}
				names.push_back(std::move(mci.name));
			}
			OpenChoiceDialog(str.c_str(), false, std::move(options),
				[game_settings = IsEditingGameSettings(bsi), names = std::move(names), file_key = std::move(file_key)](
					s32 index, const std::string& title, bool checked) {
					if (index < 0)
						return;

					auto lock = Host::GetSettingsLock();
					SettingsInterface* bsi = GetEditingSettingsInterface(game_settings);
					if (game_settings && index == 0)
					{
						bsi->DeleteValue("MemoryCards", file_key.c_str());
					}
					else
					{
						if (game_settings)
							index--;
						bsi->SetStringValue("MemoryCards", file_key.c_str(), names[index].c_str());
					}
					SetSettingsChanged(bsi);
					CloseChoiceDialog();
				});
		}

		if (MenuButton(SmallString::from_fmt(fmt::runtime(FSUI_ICONSTR_S(ICON_FA_EJECT, "Eject Card", "##eject_card_{}")), port),
				FSUI_CSTR("Removes the current card from the slot."), enabled))
		{
			bsi->SetStringValue("MemoryCards", file_key.c_str(), "");
			SetSettingsChanged(bsi);
		}
	}


	EndMenuButtons();
}

void FullscreenUI::CopyGlobalControllerSettingsToGame()
{
	SettingsInterface* dsi = GetEditingSettingsInterface(true);
	SettingsInterface* ssi = GetEditingSettingsInterface(false);

	Pad::CopyConfiguration(dsi, *ssi, true, true, false);
	USB::CopyConfiguration(dsi, *ssi, true, true);
	SetSettingsChanged(dsi);

	ShowToast(std::string(), FSUI_STR("Per-game controller configuration initialized with global settings."));
}

void FullscreenUI::ResetControllerSettings()
{
	SettingsInterface* dsi = GetEditingSettingsInterface();

	Pad::SetDefaultControllerConfig(*dsi);
	Pad::SetDefaultHotkeyConfig(*dsi);
	USB::SetDefaultConfiguration(dsi);
	ShowToast(std::string(), FSUI_STR("Controller settings reset to default."));
}

void FullscreenUI::DoLoadInputProfile()
{
	std::vector<std::string> profiles = Pad::GetInputProfileNames();
	if (profiles.empty())
	{
		ShowToast(std::string(), FSUI_STR("No input profiles available."));
		return;
	}

	ImGuiFullscreen::ChoiceDialogOptions coptions;
	coptions.reserve(profiles.size());
	for (std::string& name : profiles)
		coptions.emplace_back(std::move(name), false);
	OpenChoiceDialog(FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Load Profile"), false, std::move(coptions),
		[](s32 index, const std::string& title, bool checked) {
			if (index < 0)
				return;

			INISettingsInterface ssi(VMManager::GetInputProfilePath(title));
			if (!ssi.Load())
			{
				ShowToast(std::string(), fmt::format(FSUI_FSTR("Failed to load '{}'."), title));
				CloseChoiceDialog();
				return;
			}

			auto lock = Host::GetSettingsLock();
			SettingsInterface* dsi = GetEditingSettingsInterface();
			Pad::CopyConfiguration(dsi, ssi, true, true, IsEditingGameSettings(dsi));
			USB::CopyConfiguration(dsi, ssi, true, true);
			SetSettingsChanged(dsi);
			ShowToast(std::string(), fmt::format(FSUI_FSTR("Input profile '{}' loaded."), title));
			CloseChoiceDialog();
>>>>>>> 40babb109 (mamehooker support)
		});
	}
}

static void DrawShadowedText(
	ImDrawList* dl, std::pair<ImFont*, float> font, const ImVec2& pos, u32 col, const char* text, const char* text_end = nullptr, float wrap_width = 0.0f)
{
	ImGuiFullscreen::AddTextWithShadow(dl, font, pos, col, text, text_end, wrap_width);
}

void FullscreenUI::DrawPauseMenu(MainWindowType type)
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 display_size(ImGui::GetIO().DisplaySize);
	const ImU32 text_color = IM_COL32(UIBackgroundTextColor.x * 255, UIBackgroundTextColor.y * 255, UIBackgroundTextColor.z * 255, 255);
	dl->AddRectFilled(
		ImVec2(0.0f, 0.0f), display_size, IM_COL32(UIBackgroundColor.x * 255, UIBackgroundColor.y * 255, UIBackgroundColor.z * 255, 200));

	// title info
	{
		const float image_width = 60.0f;
		const float image_height = 90.0f;
		const std::string_view path_string(Path::GetFileName(s_current_disc_path));
		const ImVec2 title_size(
			g_large_font.first->CalcTextSizeA(g_large_font.second, std::numeric_limits<float>::max(), -1.0f, s_current_game_title.c_str()));
		const ImVec2 path_size(path_string.empty() ?
								   ImVec2(0.0f, 0.0f) :
								   g_medium_font.first->CalcTextSizeA(g_medium_font.second, std::numeric_limits<float>::max(), -1.0f,
									   path_string.data(), path_string.data() + path_string.length()));
		const ImVec2 subtitle_size(g_medium_font.first->CalcTextSizeA(
			g_medium_font.second, std::numeric_limits<float>::max(), -1.0f, s_current_game_subtitle.c_str()));

		ImVec2 title_pos(display_size.x - LayoutScale(10.0f + image_width + 20.0f) - title_size.x,
			display_size.y - LayoutScale(LAYOUT_FOOTER_HEIGHT) - LayoutScale(10.0f + image_height));
		ImVec2 path_pos(display_size.x - LayoutScale(10.0f + image_width + 20.0f) - path_size.x,
			title_pos.y + g_large_font.second + LayoutScale(4.0f));
		ImVec2 subtitle_pos(display_size.x - LayoutScale(10.0f + image_width + 20.0f) - subtitle_size.x,
			(path_string.empty() ? title_pos.y + g_large_font.second : path_pos.y + g_medium_font.second) + LayoutScale(4.0f));

		float rp_height = 0.0f;
		{
			const auto lock = Achievements::GetLock();
			const std::string& rp = Achievements::IsActive() ? Achievements::GetRichPresenceString() : std::string();

			if (!rp.empty())
			{
				const float wrap_width = LayoutScale(350.0f);
				const ImVec2 rp_size = g_medium_font.first->CalcTextSizeA(
					g_medium_font.second, std::numeric_limits<float>::max(), wrap_width, rp.data(), rp.data() + rp.length());

				// Add a small extra gap if any Rich Presence is displayed
				rp_height = rp_size.y - g_medium_font.second + LayoutScale(2.0f);

				const ImVec2 rp_pos(display_size.x - LayoutScale(20.0f + 50.0f + 20.0f) - rp_size.x,
					subtitle_pos.y + g_medium_font.second + LayoutScale(4.0f) - rp_height);

				title_pos.y -= rp_height;
				path_pos.y -= rp_height;
				subtitle_pos.y -= rp_height;

				DrawShadowedText(dl, g_medium_font, rp_pos, text_color, rp.data(), rp.data() + rp.length(), wrap_width);
			}
		}

		DrawShadowedText(dl, g_large_font, title_pos, text_color, s_current_game_title.c_str());
		if (!path_string.empty())
		{
			DrawShadowedText(dl, g_medium_font, path_pos, text_color, path_string.data(), path_string.data() + path_string.length());
		}
		DrawShadowedText(dl, g_medium_font, subtitle_pos, text_color, s_current_game_subtitle.c_str());

		const ImVec2 image_min(display_size.x - LayoutScale(10.0f + image_width),
			display_size.y - LayoutScale(LAYOUT_FOOTER_HEIGHT) - LayoutScale(10.0f + image_height) - rp_height);
		const ImVec2 image_max(image_min.x + LayoutScale(image_width), image_min.y + LayoutScale(image_height) + rp_height);
		{
			auto lock = GameList::GetLock();

			const GameList::Entry* entry = GameList::GetEntryForPath(s_current_disc_path.c_str());
			if (entry)
				DrawGameCover(entry, dl, image_min, image_max);
			else
				DrawFallbackCover(dl, image_min, image_max);
		}
	}

	// current time / play time
	{
		char buf[256];
		struct tm ltime;
		const std::time_t ctime(std::time(nullptr));
#ifdef _MSC_VER
		localtime_s(&ltime, &ctime);
#else
		localtime_r(&ctime, &ltime);
#endif
		std::strftime(buf, sizeof(buf), "%X", &ltime);

		const ImVec2 time_size(g_large_font.first->CalcTextSizeA(g_large_font.second, std::numeric_limits<float>::max(), -1.0f, buf));
		const ImVec2 time_pos(display_size.x - LayoutScale(10.0f) - time_size.x, LayoutScale(10.0f));
		DrawShadowedText(dl, g_large_font, time_pos, text_color, buf);

		if (!s_current_disc_serial.empty())
		{
			const std::time_t cached_played_time = GameList::GetCachedPlayedTimeForSerial(s_current_disc_serial);
			const std::time_t session_time = static_cast<std::time_t>(VMManager::GetSessionPlayedTime());
			const std::string played_time_str(GameList::FormatTimespan(cached_played_time + session_time, true));
			const std::string session_time_str(GameList::FormatTimespan(session_time, true));

			SmallString buf;
			buf.format(FSUI_FSTR("This Session: {}"), session_time_str);
			const ImVec2 session_size(g_medium_font.first->CalcTextSizeA(g_medium_font.second, std::numeric_limits<float>::max(), -1.0f, buf));
			const ImVec2 session_pos(
				display_size.x - LayoutScale(10.0f) - session_size.x, time_pos.y + g_large_font.second + LayoutScale(4.0f));
			DrawShadowedText(dl, g_medium_font, session_pos, text_color, buf);

			buf.format(FSUI_FSTR("All Time: {}"), played_time_str);
			const ImVec2 total_size(g_medium_font.first->CalcTextSizeA(g_medium_font.second, std::numeric_limits<float>::max(), -1.0f, buf));
			const ImVec2 total_pos(
				display_size.x - LayoutScale(10.0f) - total_size.x, session_pos.y + g_medium_font.second + LayoutScale(4.0f));
			DrawShadowedText(dl, g_medium_font, total_pos, text_color, buf);
		}
	}

	const ImVec2 window_size(LayoutScale(500.0f, LAYOUT_SCREEN_HEIGHT));
	const ImVec2 window_pos(0.0f, display_size.y - LayoutScale(LAYOUT_FOOTER_HEIGHT) - window_size.y);

	if (BeginFullscreenWindow(window_pos, window_size, "pause_menu", ImVec4(0.0f, 0.0f, 0.0f, 0.0f), 0.0f,
			ImVec2(10.0f, 10.0f), ImGuiWindowFlags_NoBackground))
	{
		static constexpr u32 submenu_item_count[] = {
			11, // None
			4, // Exit
			3, // Achievements
		};

		const bool just_focused = ResetFocusHere();
		BeginMenuButtons(submenu_item_count[static_cast<u32>(s_current_pause_submenu)], 1.0f, ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING,
			ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

		if (!ImGui::IsPopupOpen(0u, ImGuiPopupFlags_AnyPopup))
		{
			const bool up_pressed = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, ImGuiInputFlags_Repeat, ImGuiKeyOwner_NoOwner) ||
									ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, ImGuiKeyOwner_NoOwner);
			const bool down_pressed = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, ImGuiInputFlags_Repeat, ImGuiKeyOwner_NoOwner) ||
									  ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, ImGuiKeyOwner_NoOwner);

			if (up_pressed || down_pressed)
			{
				const ImGuiID current_focus_id = ImGui::GetFocusID();
				ImGuiWindow* window = ImGui::GetCurrentWindow();
				ImGuiID first_id = 0;
				ImGuiID last_id = 0;

				switch (s_current_pause_submenu)
				{
					case PauseSubMenu::None:
						first_id = ImGui::GetID(FSUI_ICONSTR(ICON_FA_PLAY, "Resume Game"));
						last_id = ImGui::GetID(FSUI_ICONSTR(ICON_FA_POWER_OFF, "Close Game"));
						break;
					case PauseSubMenu::Exit:
						first_id = ImGui::GetID(FSUI_ICONSTR(ICON_PF_BACKWARD, "Back To Pause Menu"));
						last_id = ImGui::GetID(FSUI_ICONSTR(ICON_FA_POWER_OFF, "Exit Without Saving"));
						break;
					case PauseSubMenu::Achievements:
						first_id = ImGui::GetID(FSUI_ICONSTR(ICON_PF_BACKWARD, "Back To Pause Menu"));
						last_id = ImGui::GetID(FSUI_ICONSTR(ICON_FA_STOPWATCH, "Leaderboards"));
						break;
				}

				if (first_id != 0 && last_id != 0)
				{
					if (up_pressed && current_focus_id == first_id)
						ImGui::SetFocusID(last_id, window);
					else if (down_pressed && current_focus_id == last_id)
						ImGui::SetFocusID(first_id, window);
				}
			}
		}

		switch (s_current_pause_submenu)
		{
			case PauseSubMenu::None:
			{
				// NOTE: Menu close must come first, because otherwise VM destruction options will race.
				const bool can_load_state = s_current_disc_crc != 0 && !Achievements::IsHardcoreModeActive();
				const bool can_save_state = s_current_disc_crc != 0;

				if (just_focused)
					ImGui::SetFocusID(ImGui::GetID(FSUI_ICONSTR(ICON_FA_PLAY, "Resume Game")), ImGui::GetCurrentWindow());

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_PLAY, "Resume Game"), false) || WantsToCloseMenu())
					ClosePauseMenu();

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_FORWARD_FAST, "Toggle Frame Limit"), false))
				{
					ClosePauseMenu();
					DoToggleFrameLimit();
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_ARROW_ROTATE_LEFT, "Load State"), false, can_load_state))
				{
					if (OpenSaveStateSelector(true))
						s_current_main_window = MainWindowType::None;
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_DOWNLOAD, "Save State"), false, can_save_state))
				{
					if (OpenSaveStateSelector(false))
						s_current_main_window = MainWindowType::None;
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_WRENCH, "Game Properties"), false, can_save_state))
				{
					SwitchToGameSettings();
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_TROPHY, "Achievements"), false, Achievements::HasAchievementsOrLeaderboards()))
				{
					// skip second menu and go straight to cheevos if there's no lbs
					if (!Achievements::HasLeaderboards())
						OpenAchievementsWindow();
					else
						OpenPauseSubMenu(PauseSubMenu::Achievements);
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_CAMERA, "Save Screenshot"), false))
				{
					GSQueueSnapshot(std::string());
					ClosePauseMenu();
				}

				if (ActiveButton(GSIsHardwareRenderer() ? (FSUI_ICONSTR(ICON_FA_PAINTBRUSH, "Switch To Software Renderer")) :
														  (FSUI_ICONSTR(ICON_FA_PAINTBRUSH, "Switch To Hardware Renderer")),
						false))
				{
					ClosePauseMenu();
					DoToggleSoftwareRenderer();
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_COMPACT_DISC, "Change Disc"), false))
				{
					s_current_main_window = MainWindowType::None;
					RequestChangeDisc();
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_SLIDERS, "Settings"), false))
					SwitchToSettings();

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_POWER_OFF, "Close Game"), false))
				{
					// skip submenu when we can't save anyway
					if (!can_save_state)
						RequestShutdown(false);
					else
						OpenPauseSubMenu(PauseSubMenu::Exit);
				}
			}
			break;

			case PauseSubMenu::Exit:
			{
				if (just_focused)
				{
					ImGui::SetFocusID(ImGui::GetID(FSUI_ICONSTR(ICON_FA_POWER_OFF, "Exit Without Saving")), ImGui::GetCurrentWindow());
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_PF_BACKWARD, "Back To Pause Menu"), false) || WantsToCloseMenu())
					OpenPauseSubMenu(PauseSubMenu::None);

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_ARROWS_SPIN, "Reset System"), false))
				{
					RequestReset();
				}

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_FLOPPY_DISK, "Exit And Save State"), false))
					RequestShutdown(true);

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_POWER_OFF, "Exit Without Saving"), false))
					RequestShutdown(false);
			}
			break;

			case PauseSubMenu::Achievements:
			{
				if (just_focused)
					ImGui::SetFocusID(ImGui::GetID(FSUI_ICONSTR(ICON_PF_BACKWARD, "Back To Pause Menu")), ImGui::GetCurrentWindow());

				if (ActiveButton(FSUI_ICONSTR(ICON_PF_BACKWARD, "Back To Pause Menu"), false) || WantsToCloseMenu())
					OpenPauseSubMenu(PauseSubMenu::None);

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_TROPHY, "Achievements"), false))
					OpenAchievementsWindow();

				if (ActiveButton(FSUI_ICONSTR(ICON_FA_STOPWATCH, "Leaderboards"), false))
					OpenLeaderboardsWindow();
			}
			break;
		}

		EndMenuButtons();

		EndFullscreenWindow();
	}

	// Primed achievements must come first, because we don't want the pause screen to be behind them.
	if (Achievements::HasAchievementsOrLeaderboards())
		Achievements::DrawPauseMenuOverlays();

	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const auto glyphs = GetGamepadGlyphs();
		SetFullscreenFooterText(std::array{
			std::make_pair(glyphs.dpad_ud, FSUI_VSTR("Change Selection")),
			std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Select")),
			std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Return To Game")),
		});
	}
	else
	{
		SetFullscreenFooterText(std::array{
			std::make_pair(ICON_PF_ARROW_UP ICON_PF_ARROW_DOWN, FSUI_VSTR("Change Selection")),
			std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Select")),
			std::make_pair(ICON_PF_ESC, FSUI_VSTR("Return To Game")),
		});
	}
}

void FullscreenUI::InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot)
{
	li->title = fmt::format("{}##game_slot_{}", TinyString::from_format(FSUI_FSTR("Save Slot {0}"), slot), slot);
	li->summary = FSUI_STR("No save present in this slot.");
	li->path = {};
	li->timestamp = 0;
	li->slot = slot;
	li->preview_texture = {};
}

bool FullscreenUI::InitializeSaveStateListEntry(
	SaveStateListEntry* li, const std::string& title, const std::string& serial, u32 crc, s32 slot, bool backup)
{
	std::string filename(VMManager::GetSaveStateFileName(serial.c_str(), crc, slot, backup));
	FILESYSTEM_STAT_DATA sd;
	if (filename.empty() || !FileSystem::StatFile(filename.c_str(), &sd))
	{
		InitializePlaceholderSaveStateListEntry(li, slot);
		return false;
	}

	li->title = fmt::format("{}##game_slot_{}", TinyString::from_format(FSUI_FSTR("{0} Slot {1}"), backup ? "Backup Save" : "Save", slot), slot);
	li->summary = fmt::format(FSUI_FSTR("Saved {}"), TimeToPrintableString(sd.ModificationTime));
	li->slot = slot;
	li->timestamp = sd.ModificationTime;
	li->path = std::move(filename);

	li->preview_texture.reset();

	u32 screenshot_width, screenshot_height;
	std::vector<u32> screenshot_pixels;
	if (SaveState_ReadScreenshot(li->path, &screenshot_width, &screenshot_height, &screenshot_pixels))
	{
		li->preview_texture =
			std::unique_ptr<GSTexture>(g_gs_device->CreateTexture(screenshot_width, screenshot_height, 1, GSTexture::Format::Color));
		if (!li->preview_texture || !li->preview_texture->Update(GSVector4i(0, 0, screenshot_width, screenshot_height),
										screenshot_pixels.data(), sizeof(u32) * screenshot_width))
		{
			Console.Error("Failed to upload save state image to GPU");
			if (li->preview_texture)
				g_gs_device->Recycle(li->preview_texture.release());
		}
	}

	return true;
}

void FullscreenUI::ClearSaveStateEntryList()
{
	for (SaveStateListEntry& entry : s_save_state_selector_slots)
	{
		if (entry.preview_texture)
			s_cleanup_textures.push_back(std::move(entry.preview_texture));
	}
	s_save_state_selector_slots.clear();
}

u32 FullscreenUI::PopulateSaveStateListEntries(const std::string& title, const std::string& serial, u32 crc)
{
	ClearSaveStateEntryList();

	for (s32 i = 1; i <= VMManager::NUM_SAVE_STATE_SLOTS; i++)
	{
		SaveStateListEntry li;
		if (InitializeSaveStateListEntry(&li, title, serial, crc, i) || !s_save_state_selector_loading)
			s_save_state_selector_slots.push_back(std::move(li));

		if (s_save_state_selector_loading)
		{
			SaveStateListEntry bli;
			if (InitializeSaveStateListEntry(&bli, title, serial, crc, i, true))
				s_save_state_selector_slots.push_back(std::move(bli));
		}
	}

	return static_cast<u32>(s_save_state_selector_slots.size());
}

bool FullscreenUI::OpenLoadStateSelectorForGame(const std::string& game_path)
{
	auto lock = GameList::GetLock();
	const GameList::Entry* entry = GameList::GetEntryForPath(game_path.c_str());
	if (entry)
	{
		s_save_state_selector_loading = true;
		if (PopulateSaveStateListEntries(entry->title.c_str(), entry->serial.c_str(), entry->crc) > 0)
		{
			s_save_state_selector_open = true;
			s_save_state_selector_resuming = false;
			s_save_state_selector_game_path = game_path;
			return true;
		}
	}

	ShowToast({}, FSUI_STR("No save states found."), 5.0f);
	return false;
}

bool FullscreenUI::OpenSaveStateSelector(bool is_loading)
{
	s_save_state_selector_game_path = {};
	s_save_state_selector_loading = is_loading;
	s_save_state_selector_resuming = false;
	if (PopulateSaveStateListEntries(s_current_game_title.c_str(), s_current_disc_serial.c_str(), s_current_disc_crc) > 0)
	{
		s_save_state_selector_open = true;
		return true;
	}

	ShowToast({}, FSUI_STR("No save states found."), 5.0f);
	return false;
}

void FullscreenUI::CloseSaveStateSelector()
{
	ClearSaveStateEntryList();
	s_save_state_selector_open = false;
	s_save_state_selector_submenu_index = -1;
	s_save_state_selector_loading = false;
	s_save_state_selector_resuming = false;
	s_save_state_selector_game_path = {};
}

void FullscreenUI::DrawSaveStateSelector(bool is_loading)
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(io.DisplaySize - LayoutScale(0.0f, LAYOUT_FOOTER_HEIGHT));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

	const char* window_title = is_loading ? FSUI_CSTR("Load State") : FSUI_CSTR("Save State");
	ImGui::OpenPopup(window_title);

	bool is_open = true;
	const bool valid = ImGui::BeginPopupModal(window_title, &is_open,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoBackground);
	if (!valid || !is_open)
	{
		if (valid)
			ImGui::EndPopup();

		ImGui::PopStyleVar(5);
		if (!is_open)
		{
			CloseSaveStateSelector();
			ReturnToPreviousWindow();
		}
		return;
	}

	const ImVec2 heading_size =
		ImVec2(io.DisplaySize.x, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY) +
									 (LayoutScale(LAYOUT_MENU_BUTTON_Y_PADDING) * 2.0f) + LayoutScale(2.0f));

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ModAlpha(UIPrimaryColor, 0.9f));

	if (ImGui::BeginChild("state_titlebar", heading_size, ImGuiChildFlags_NavFlattened, 0))
	{
		BeginNavBar();
		if (NavButton(ICON_PF_BACKWARD, true, true))
		{
			CloseSaveStateSelector();
			ReturnToPreviousWindow();
		}

		NavTitle(is_loading ? FSUI_CSTR("Load State") : FSUI_CSTR("Save State"));
		EndNavBar();
		ImGui::EndChild();
	}

	ImGui::PopStyleColor();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ModAlpha(UIBackgroundColor, 0.9f));
	ImGui::SetCursorPos(ImVec2(0.0f, heading_size.y));

	bool close_handled = false;
	if (s_save_state_selector_open &&
		ImGui::BeginChild("state_list", ImVec2(io.DisplaySize.x, io.DisplaySize.y - LayoutScale(LAYOUT_FOOTER_HEIGHT) - heading_size.y),
			ImGuiChildFlags_NavFlattened, 0))
	{
		BeginMenuButtons();

		const ImGuiStyle& style = ImGui::GetStyle();

		const float title_spacing = LayoutScale(10.0f);
		const float summary_spacing = LayoutScale(4.0f);
		const float item_spacing = LayoutScale(20.0f);
		const float item_width_with_spacing = std::floor(LayoutScale(LAYOUT_SCREEN_WIDTH / 4.0f));
		const float item_width = item_width_with_spacing - item_spacing;
		const float image_width = item_width - (style.FramePadding.x * 2.0f);
		const float image_height = image_width / 1.33f;
		const ImVec2 image_size(image_width, image_height);
		const float item_height = (style.FramePadding.y * 2.0f) + image_height + title_spacing + g_large_font.second + summary_spacing +
								  g_medium_font.second;
		const ImVec2 item_size(item_width, item_height);
		const u32 grid_count_x = std::floor(ImGui::GetWindowWidth() / item_width_with_spacing);
		const float start_x =
			(static_cast<float>(ImGui::GetWindowWidth()) - (item_width_with_spacing * static_cast<float>(grid_count_x))) * 0.5f;

		u32 grid_x = 0;
		for (u32 i = 0; i < s_save_state_selector_slots.size();)
		{
			if (i == 0)
				ResetFocusHere();

			if (static_cast<s32>(i) == s_save_state_selector_submenu_index)
			{
				SaveStateListEntry& entry = s_save_state_selector_slots[i];

				// can't use a choice dialog here, because we're already in a modal...
				ImGuiFullscreen::PushResetLayout();
				ImGui::PushFont(g_large_font.first, g_large_font.second);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING, LAYOUT_MENU_BUTTON_Y_PADDING));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, UIPrimaryTextColor);
				ImGui::PushStyleColor(ImGuiCol_TitleBg, UIPrimaryDarkColor);
				ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIPrimaryColor);
				ImGui::PushStyleColor(ImGuiCol_PopupBg, UIPopupBackgroundColor);

				const float width = LayoutScale(600.0f);
				const float title_height =
					g_large_font.second + ImGui::GetStyle().FramePadding.y * 2.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
				const float height =
					title_height + LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY + (LAYOUT_MENU_BUTTON_Y_PADDING * 2.0f)) * 3.0f;
				ImGui::SetNextWindowSize(ImVec2(width, height));
				ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
				ImGui::OpenPopup(entry.title.c_str());

				// don't let the back button flow through to the main window
				bool submenu_open = !WantsToCloseMenu();
				close_handled ^= submenu_open;

				bool closed = false;
				if (ImGui::BeginPopupModal(
						entry.title.c_str(), &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, UIBackgroundTextColor);

					BeginMenuButtons();

					if (ActiveButton(
							is_loading ? FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Load State") : FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Save State"),
							false, is_loading ? !Achievements::IsHardcoreModeActive() : true, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY))
					{
						if (is_loading)
							DoLoadState(std::move(entry.path), entry.slot, false);
						else
							DoSaveState(entry.slot);

						CloseSaveStateSelector();
						ReturnToMainWindow();
						closed = true;
					}

					if (ActiveButton(FSUI_ICONSTR(ICON_FA_TRASH, "Delete Save"), false, true, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY))
					{
						if (!FileSystem::FileExists(entry.path.c_str()))
						{
							ShowToast({}, fmt::format(FSUI_FSTR("{} does not exist."), ImGuiFullscreen::RemoveHash(entry.title)));
							is_open = true;
						}
						else if (FileSystem::DeleteFilePath(entry.path.c_str()))
						{
							ShowToast({}, fmt::format(FSUI_FSTR("{} deleted."), ImGuiFullscreen::RemoveHash(entry.title)));
							if (s_save_state_selector_loading)
								s_save_state_selector_slots.erase(s_save_state_selector_slots.begin() + i);
							else
								InitializePlaceholderSaveStateListEntry(&entry, entry.slot);

							// Close if this was the last state.
							if (s_save_state_selector_slots.empty())
							{
								CloseSaveStateSelector();
								ReturnToMainWindow();
								closed = true;
							}
							else
							{
								is_open = false;
							}
						}
						else
						{
							ShowToast({}, fmt::format(FSUI_FSTR("Failed to delete {}."), ImGuiFullscreen::RemoveHash(entry.title)));
							is_open = false;
						}
					}

					if (ActiveButton(FSUI_ICONSTR(ICON_FA_SQUARE_XMARK, "Close Menu"), false, true, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY))
					{
						is_open = false;
					}

					EndMenuButtons();

					ImGui::PopStyleColor();
					ImGui::EndPopup();
				}
				if (!is_open)
				{
					s_save_state_selector_submenu_index = -1;
					if (!closed)
						QueueResetFocus(FocusResetType::WindowChanged);
				}

				ImGui::PopStyleColor(4);
				ImGui::PopStyleVar(3);
				ImGui::PopFont();
				ImGuiFullscreen::PopResetLayout();

				if (closed || i >= s_save_state_selector_slots.size())
					break;
			}

			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window->SkipItems)
			{
				i++;
				continue;
			}

			if (grid_x == grid_count_x)
			{
				grid_x = 0;
				ImGui::SetCursorPosX(start_x);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + item_spacing);
			}
			else
			{
				ImGui::SameLine(start_x + static_cast<float>(grid_x) * (item_width + item_spacing));
			}

			const SaveStateListEntry& entry = s_save_state_selector_slots[i];
			const ImGuiID id = window->GetID(static_cast<int>(i));
			const ImVec2 pos(window->DC.CursorPos);
			ImRect bb(pos, pos + item_size);
			ImGui::ItemSize(item_size);
			if (ImGui::ItemAdd(bb, id))
			{
				bool held;
				bool hovered;
				bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);
				if (hovered)
				{
					const ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered, 0.7f);

					const float t = std::min<float>(std::abs(std::sin(ImGui::GetTime() * 0.75) * 1.1), 1.0f);
					ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_Border, t));

					ImGuiFullscreen::DrawMenuButtonFrame(bb.Min, bb.Max, col, true, 0.0f);

					ImGui::PopStyleColor();
				}

				bb.Min += style.FramePadding;
				bb.Max -= style.FramePadding;

				const GSTexture* const screenshot = entry.preview_texture ? entry.preview_texture.get() : GetPlaceholderTexture().get();
				const ImRect image_rect(CenterImage(ImRect(bb.Min, bb.Min + image_size),
					ImVec2(static_cast<float>(screenshot->GetWidth()), static_cast<float>(screenshot->GetHeight()))));

				ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(screenshot->GetNativeHandle()),
					image_rect.Min, image_rect.Max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));

				const ImVec2 title_pos(bb.Min.x, bb.Min.y + image_height + title_spacing);
				const ImRect title_bb(title_pos, ImVec2(bb.Max.x, title_pos.y + g_large_font.second));
				ImGui::PushFont(g_large_font.first, g_large_font.second);
				ImGuiFullscreen::RenderTextClippedWithShadow(
					title_bb.Min, title_bb.Max, entry.title.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &title_bb);
				ImGui::PopFont();

				if (!entry.summary.empty())
				{
					const ImVec2 summary_pos(bb.Min.x, title_pos.y + g_large_font.second + summary_spacing);
					const ImRect summary_bb(summary_pos, ImVec2(bb.Max.x, summary_pos.y + g_medium_font.second));
					ImGui::PushFont(g_medium_font.first, g_medium_font.second);
					ImGuiFullscreen::RenderTextClippedWithShadow(
						summary_bb.Min, summary_bb.Max, entry.summary.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &summary_bb);
					ImGui::PopFont();
				}

				if (pressed)
				{
					if (is_loading)
						DoLoadState(entry.path, entry.slot, false);
					else
						DoSaveState(entry.slot);

					CloseSaveStateSelector();
					ReturnToMainWindow();
					break;
				}
				else if (hovered && (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::Shortcut(ImGuiKey_NavGamepadMenu) ||
										ImGui::Shortcut(ImGuiKey_F1)))
				{
					s_save_state_selector_submenu_index = static_cast<s32>(i);
				}
			}

			grid_x++;
			i++;
		}

		EndMenuButtons();
		ImGui::EndChild();
	}

	ImGui::PopStyleColor();

	ImGui::EndPopup();
	ImGui::PopStyleVar(5);

	if (!close_handled && WantsToCloseMenu())
	{
		CloseSaveStateSelector();
		ReturnToPreviousWindow();
	}
	else
	{
		if (IsGamepadInputSource())
		{
			const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
			const bool swapNorthWest = ImGuiManager::IsGamepadNorthWestSwapped();
			const auto glyphs = GetGamepadGlyphs();
			SetFullscreenFooterText(std::array{
				std::make_pair(glyphs.dpad, FSUI_VSTR("Select State")),
				std::make_pair(swapNorthWest ? glyphs.north : glyphs.west, FSUI_VSTR("Options")),
				std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Load/Save State")),
				std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Cancel")),
			});
		}
		else
		{
			SetFullscreenFooterText(std::array{
				std::make_pair(ICON_PF_ARROW_UP ICON_PF_ARROW_DOWN ICON_PF_ARROW_LEFT ICON_PF_ARROW_RIGHT, FSUI_VSTR("Select State")),
				std::make_pair(ICON_PF_F1, FSUI_VSTR("Options")),
				std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Load/Save State")),
				std::make_pair(ICON_PF_ESC, FSUI_VSTR("Cancel")),
			});
		}
	}
}

bool FullscreenUI::OpenLoadStateSelectorForGameResume(const GameList::Entry* entry)
{
	SaveStateListEntry slentry;
	if (!InitializeSaveStateListEntry(&slentry, entry->title, entry->serial, entry->crc, -1))
		return false;

	CloseSaveStateSelector();
	s_save_state_selector_slots.push_back(std::move(slentry));
	s_save_state_selector_game_path = entry->path;
	s_save_state_selector_loading = true;
	s_save_state_selector_open = true;
	s_save_state_selector_resuming = true;
	return true;
}

void FullscreenUI::DrawResumeStateSelector()
{
	ImGui::SetNextWindowSize(LayoutScale(800.0f, 600.0f));
	ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::OpenPopup(FSUI_CSTR("Load Resume State"));

	ImGui::PushFont(g_large_font.first, g_large_font.second);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(20.0f, 20.0f));

	bool is_open = true;
	if (ImGui::BeginPopupModal(FSUI_CSTR("Load Resume State"), &is_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
	{
		const SaveStateListEntry& entry = s_save_state_selector_slots.front();
		ImGui::TextWrapped(FSUI_CSTR("A resume save state created at %s was found.\n\nDo you want to load this save and continue?"),
			TimeToPrintableString(entry.timestamp).c_str());

		const GSTexture* image = entry.preview_texture ? entry.preview_texture.get() : GetPlaceholderTexture().get();
		const float image_height = LayoutScale(250.0f);
		const float image_width = image_height * (static_cast<float>(image->GetWidth()) / static_cast<float>(image->GetHeight()));
		const ImVec2 pos(ImGui::GetCursorScreenPos() +
						 ImVec2((ImGui::GetCurrentWindow()->WorkRect.GetWidth() - image_width) * 0.5f, LayoutScale(20.0f)));
		const ImRect image_bb(pos, pos + ImVec2(image_width, image_height));
		ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(entry.preview_texture ? entry.preview_texture->GetNativeHandle() :
																								   GetPlaceholderTexture()->GetNativeHandle()),
			image_bb.Min, image_bb.Max);

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + image_height + LayoutScale(40.0f));

		BeginMenuButtons();

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_PLAY, "Load State"), false))
		{
			DoStartPath(s_save_state_selector_game_path, -1);
			is_open = false;
		}

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_COMPACT_DISC, "Default Boot"), false))
		{
			DoStartPath(s_save_state_selector_game_path);
			is_open = false;
		}

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_TRASH, "Delete State"), false))
		{
			if (FileSystem::DeleteFilePath(entry.path.c_str()))
			{
				DoStartPath(s_save_state_selector_game_path);
				is_open = false;
			}
			else
			{
				ShowToast(std::string(), FSUI_STR("Failed to delete save state."));
			}
		}

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_SQUARE_XMARK, "Cancel"), false) || WantsToCloseMenu())
		{
			ImGui::CloseCurrentPopup();
			is_open = false;
		}
		EndMenuButtons();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
	ImGui::PopFont();

	if (!is_open)
	{
		ClearSaveStateEntryList();
		s_save_state_selector_open = false;
		s_save_state_selector_loading = false;
		s_save_state_selector_resuming = false;
		s_save_state_selector_game_path = {};
	}
	else
	{
		SetStandardSelectionFooterText(false);
	}
}

void FullscreenUI::DoLoadState(std::string path, std::optional<s32> slot, bool backup)
{
	std::string boot_path = s_save_state_selector_game_path;
	Host::RunOnCPUThread([boot_path = std::move(boot_path), path = std::move(path), slot, backup]() {
		if (VMManager::HasValidVM())
		{
			Error error;
			if (!VMManager::LoadState(path.c_str(), &error))
			{
				ReportStateLoadError(error.GetDescription(), slot, backup);
				return;
			}

			if (!boot_path.empty() && VMManager::GetDiscPath() != boot_path)
				VMManager::ChangeDisc(CDVD_SourceType::Iso, std::move(boot_path));
		}
		else
		{
			VMBootParameters params;
			params.filename = std::move(boot_path);
			params.save_state = std::move(path);
			DoVMInitialize(params, false);
		}
	});
}

void FullscreenUI::DoSaveState(s32 slot)
{
	Host::RunOnCPUThread([slot]() {
		VMManager::SaveStateToSlot(slot, true, [slot](const std::string& error) {
			ReportStateSaveError(error, slot);
		});
	});
}

void FullscreenUI::PopulateGameListEntryList()
{
	const int sort = Host::GetBaseIntSettingValue("UI", "FullscreenUIGameSort", 0);
	const bool reverse = Host::GetBaseBoolSettingValue("UI", "FullscreenUIGameSortReverse", false);
	static int s_last_sort = -1;
	static bool s_last_reverse = false;
	static bool s_last_prefer_eng = false;
	static std::vector<const GameList::Entry*> s_last_unsorted_entries;

	// Sort can be expensive, try to avoid when possible
	const u32 count = GameList::GetEntryCount();
	bool needs_update = sort != s_last_sort || reverse != s_last_reverse || s_last_prefer_eng != s_prefer_english_titles;
	needs_update |= count != s_last_unsorted_entries.size();
	if (!needs_update)
	{
		for (u32 i = 0; i < count; i++)
		{
			if (GameList::GetEntryByIndex(i) != s_last_unsorted_entries[i])
			{
				needs_update = true;
				break;
			}
		}
	}

	if (!needs_update)
		return;

	s_last_sort = sort;
	s_last_reverse = reverse;
	s_last_prefer_eng = s_prefer_english_titles;

	s_game_list_sorted_entries.resize(count);
	s_last_unsorted_entries.resize(count);
	for (u32 i = 0; i < count; i++)
	{
		s_game_list_sorted_entries[i] = GameList::GetEntryByIndex(i);
		s_last_unsorted_entries[i] = s_game_list_sorted_entries[i];
	}

	std::sort(s_game_list_sorted_entries.begin(), s_game_list_sorted_entries.end(),
		[sort, reverse](const GameList::Entry* lhs, const GameList::Entry* rhs) {
			switch (sort)
			{
				case 0: // Type
				{
					if (lhs->type != rhs->type)
						return reverse ? (lhs->type > rhs->type) : (lhs->type < rhs->type);
				}
				break;

				case 1: // Serial
				{
					if (lhs->serial != rhs->serial)
						return reverse ? (lhs->serial > rhs->serial) : (lhs->serial < rhs->serial);
				}
				break;

				case 2: // Title
					break;

				case 3: // File Title
				{
					const std::string_view lhs_title(Path::GetFileTitle(lhs->path));
					const std::string_view rhs_title(Path::GetFileTitle(rhs->path));
					const int res =
						StringUtil::Strncasecmp(lhs_title.data(), rhs_title.data(), std::min(lhs_title.size(), rhs_title.size()));
					if (res != 0)
						return reverse ? (res > 0) : (res < 0);
				}
				break;

				case 4: // CRC
				{
					if (lhs->crc != rhs->crc)
						return reverse ? (lhs->crc > rhs->crc) : (lhs->crc < rhs->crc);
				}
				break;

				case 5: // Time Played
				{
					if (lhs->total_played_time != rhs->total_played_time)
					{
						return reverse ? (lhs->total_played_time > rhs->total_played_time) :
										 (lhs->total_played_time < rhs->total_played_time);
					}
				}
				break;

				case 6: // Last Played (reversed by default)
				{
					if (lhs->last_played_time != rhs->last_played_time)
					{
						return reverse ? (lhs->last_played_time < rhs->last_played_time) : (lhs->last_played_time > rhs->last_played_time);
					}
				}
				break;

				case 7: // Size
				{
					if (lhs->total_size != rhs->total_size)
					{
						return reverse ? (lhs->total_size > rhs->total_size) : (lhs->total_size < rhs->total_size);
					}
				}
				break;
			}

			// fallback to title when all else is equal
			const int res = Host::LocaleSensitiveCompare(lhs->GetTitleSort(s_prefer_english_titles), rhs->GetTitleSort(s_prefer_english_titles));
			return reverse ? (res > 0) : (res < 0);
		});
}

void FullscreenUI::DrawGameListWindow()
{
	auto game_list_lock = GameList::GetLock();
	PopulateGameListEntryList();

	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 heading_size =
		ImVec2(io.DisplaySize.x, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY) +
									 (LayoutScale(LAYOUT_MENU_BUTTON_Y_PADDING) * 2.0f) + LayoutScale(2.0f));

	const float bg_alpha = VMManager::HasValidVM() ? 0.90f : 1.0f;

	if (BeginFullscreenWindow(ImVec2(0.0f, 0.0f), heading_size, "gamelist_view", MulAlpha(UIPrimaryColor, bg_alpha)))
	{
		static constexpr float ITEM_WIDTH = 25.0f;
		static constexpr const char* icons[] = {ICON_FA_BORDER_ALL, ICON_FA_LIST};
		static constexpr const char* titles[] = {FSUI_NSTR("Game Grid"), FSUI_NSTR("Game List")};
		static constexpr u32 count = std::size(titles);

		BeginNavBar();

		if (NavButton(ICON_PF_BACKWARD, true, true))
			SwitchToLanding();

		NavTitle(Host::TranslateToCString(TR_CONTEXT, titles[static_cast<u32>(s_game_list_view)]));
		RightAlignNavButtons(count, ITEM_WIDTH, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

		for (u32 i = 0; i < count; i++)
		{
			if (NavButton(icons[i], static_cast<GameListView>(i) == s_game_list_view, true, ITEM_WIDTH,
					LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY))
			{
				s_game_list_view = static_cast<GameListView>(i);
			}
		}

		EndNavBar();
	}

	EndFullscreenWindow();

	if (ImGui::IsKeyPressed(ImGuiKey_NavGamepadContextMenu, false) || ImGui::IsKeyPressed(ImGuiKey_F1, false))
	{
		s_game_list_view = (s_game_list_view == GameListView::Grid) ? GameListView::List : GameListView::Grid;
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_F2))
	{
		s_current_main_window = MainWindowType::GameListSettings;
		QueueResetFocus(FocusResetType::WindowChanged);
	}

	switch (s_game_list_view)
	{
		case GameListView::Grid:
			DrawGameGrid(heading_size);
			break;
		case GameListView::List:
			DrawGameList(heading_size);
			break;
		default:
			break;
	}

	if (VMManager::GetState() != VMState::Shutdown)
	{
		// Dummy window to prevent interacting with the game list while loading.
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowBgAlpha(0.25f);
		ImGui::Begin("##dummy", nullptr, ImGuiWindowFlags_NoDecoration);
		ImGui::End();
		ImGui::PopStyleColor();
	}

	if (IsGamepadInputSource())
	{
		const bool circleOK = ImGui::GetIO().ConfigNavSwapGamepadButtons;
		const bool swapNorthWest = ImGuiManager::IsGamepadNorthWestSwapped();
		const auto glyphs = GetGamepadGlyphs();
		SetFullscreenFooterText(std::array{
			std::make_pair(glyphs.dpad, FSUI_VSTR("Select Game")),
			std::make_pair(glyphs.start, FSUI_VSTR("Settings")),
			std::make_pair(swapNorthWest ? glyphs.west : glyphs.north, FSUI_VSTR("Change View")),
			std::make_pair(swapNorthWest ? glyphs.north : glyphs.west, FSUI_VSTR("Launch Options")),
			std::make_pair(glyphs.confirm(circleOK), FSUI_VSTR("Start Game")),
			std::make_pair(glyphs.cancel(circleOK), FSUI_VSTR("Back")),
		});
	}
	else
	{
		SetFullscreenFooterText(std::array{
			std::make_pair(ICON_PF_ARROW_UP ICON_PF_ARROW_DOWN ICON_PF_ARROW_LEFT ICON_PF_ARROW_RIGHT, FSUI_VSTR("Select Game")),
			std::make_pair(ICON_PF_F1, FSUI_VSTR("Change View")),
			std::make_pair(ICON_PF_F2, FSUI_VSTR("Settings")),
			std::make_pair(ICON_PF_F3, FSUI_VSTR("Launch Options")),
			std::make_pair(ICON_PF_ENTER, FSUI_VSTR("Start Game")),
			std::make_pair(ICON_PF_ESC, FSUI_VSTR("Back")),
		});
	}
}

std::string_view FullscreenUI::TrimString(const std::pair<ImFont*, float>& font, std::string_view str, float available_space)
{
	const char* beg = str.data();
	const char* end = beg + str.size();
	float full_width = font.first->CalcTextSizeA(font.second, INFINITY, 0, beg, end).x;
	if (full_width <= available_space)
		return str;
	float ellipsis_width = ImGui::CalcTextSize(g_ellipsis).x;
	const char* trimmed = end;
	font.first->CalcTextSizeA(font.second, available_space - ellipsis_width, 0, beg, end, &trimmed);
	return std::string_view(beg, trimmed);
}

void FullscreenUI::DrawGameList(const ImVec2& heading_size)
{
	ImGui::PushStyleColor(ImGuiCol_WindowBg, UIBackgroundColor);

	if (!BeginFullscreenColumns(nullptr, heading_size.y, true, true))
	{
		EndFullscreenColumns();
		ImGui::PopStyleColor();
		return;
	}

	if (!AreAnyDialogsOpen() && WantsToCloseMenu())
		SwitchToLanding();

	const GameList::Entry* selected_entry = nullptr;

	if (BeginFullscreenColumnWindow(0.0f, -530.0f, "game_list_entries"))
	{
		const ImVec2 image_size(LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT * 0.68f, LAYOUT_MENU_BUTTON_HEIGHT));

		ResetFocusHere();

		BeginMenuButtons();

		// TODO: replace with something not heap allocating
		std::string summary;

		for (const GameList::Entry* entry : s_game_list_sorted_entries)
		{
			ImRect bb;
			bool visible, hovered;
			bool pressed = MenuButtonFrame(entry->path.c_str(), true, LAYOUT_MENU_BUTTON_HEIGHT, &visible, &hovered, &bb.Min, &bb.Max);
			if (!visible)
				continue;

			summary.clear();
			if (entry->serial.empty())
				fmt::format_to(std::back_inserter(summary), "{} - ", GameList::RegionToString(entry->region, true));
			else
				fmt::format_to(std::back_inserter(summary), "{} - {} - ", entry->serial, GameList::RegionToString(entry->region, true));

			const std::string_view filename(Path::GetFileName(entry->path));
			summary.append(filename);

			DrawGameCover(entry, ImGui::GetWindowDrawList(), bb.Min, bb.Min + image_size);

			const float midpoint = bb.Min.y + GetLineHeight(g_large_font) + LayoutScale(4.0f);
			const float text_start_x = bb.Min.x + image_size.x + LayoutScale(15.0f);
			const ImRect title_bb(ImVec2(text_start_x, bb.Min.y), ImVec2(bb.Max.x, midpoint));
			const ImRect summary_bb(ImVec2(text_start_x, midpoint), bb.Max);
			const std::string& title = entry->GetTitle(s_prefer_english_titles);

			ImGui::PushFont(g_large_font.first, g_large_font.second);
			ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, title.c_str(), title.c_str() + title.size(), nullptr,
				ImVec2(0.0f, 0.0f), &title_bb);
			ImGui::PopFont();

			if (!summary.empty())
			{
				ImGui::PushFont(g_medium_font.first, g_medium_font.second);
				ImGui::RenderTextClipped(
					summary_bb.Min, summary_bb.Max, summary.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &summary_bb);
				ImGui::PopFont();
			}

			if (pressed)
				HandleGameListActivate(entry);

			if (hovered)
				selected_entry = entry;

			if (selected_entry &&
				(ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::Shortcut(ImGuiKey_NavGamepadMenu) ||
					ImGui::Shortcut(ImGuiKey_F3)))
			{
				HandleGameListOptions(selected_entry);
			}
		}

		EndMenuButtons();
	}
	EndFullscreenColumnWindow();

	if (BeginFullscreenColumnWindow(-530.0f, 0.0f, "game_list_info", UIPrimaryDarkColor))
	{
		const float img_padding_y = LayoutScale(20.0f);
		// Spacing between each text item
		const float text_spacing_y = LayoutScale(8.0f);
		// Space between title/serial and details, is in addition to text_spacing_y
		const float title_padding_below_y = LayoutScale(12.0f);

		// Estimate how much space is needed for text
		// Do this even when nothing is selected, to ensure cover/icon is in a consistant size/position
		const float title_detail_height =
			LayoutScale(LAYOUT_LARGE_FONT_SIZE) + text_spacing_y + // Title
			LayoutScale(LAYOUT_MEDIUM_FONT_SIZE) + text_spacing_y + // Serial
			title_padding_below_y +
			7.0f * (LayoutScale(LAYOUT_MEDIUM_FONT_SIZE) + text_spacing_y) + // File, CRC, Region, Compat, Time/Last Played, Size
			LayoutScale(12.0f); // Extra padding

		// Limit cover height to avoid pushing text off the screen
		const ImGuiWindow* window = ImGui::GetCurrentWindow();
		// Based on ImGui code for WorkRect, with scrolling logic removed
		const float window_height = std::trunc(window->InnerRect.GetHeight() - 2.0f * std::max(window->WindowPadding.y, window->WindowBorderSize));

		const float free_height = window_height - title_detail_height;
		const float img_height = std::min(free_height - 2.0f * img_padding_y, LayoutScale(400.0f));

		const ImVec2 image_size = ImVec2(LayoutScale(275.0f), img_height);
		ImGui::SetCursorPos(ImVec2(LayoutScale(128.0f), img_padding_y));

		if (selected_entry)
			DrawGameCover(selected_entry, image_size);
		else
			DrawFallbackCover(image_size);

		const float work_width = window->WorkRect.GetWidth();
		const float start_x = LayoutScale(50.0f);
		const float text_y = img_height + 2.0f * img_padding_y;
		float text_width;

		PushPrimaryColor();
		ImGui::SetCursorPos(ImVec2(start_x, text_y));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, text_spacing_y));
		ImGui::PushTextWrapPos(LayoutScale(490.0f));
		ImGui::BeginGroup();

		if (selected_entry)
		{
			// title
			ImGui::PushFont(g_large_font.first, g_large_font.second);
			const std::string_view full_title(selected_entry->GetTitle(s_prefer_english_titles));
			std::string_view title = TrimString(g_large_font, full_title, work_width);
			text_width = ImGui::CalcTextSize(title.data(), title.data() + title.length(), false, work_width).x;
			if (title.length() != full_title.length())
				text_width += ImGui::CalcTextSize(g_ellipsis).x;
			ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
			ImGui::TextWrapped(
				"%.*s%s", static_cast<int>(title.size()), title.data(), (title.length() == full_title.length()) ? "" : g_ellipsis);
			ImGui::PopFont();

			ImGui::PushFont(g_medium_font.first, g_medium_font.second);

			// code
			text_width = ImGui::CalcTextSize(selected_entry->serial.c_str(), nullptr, false, work_width).x;
			ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
			ImGui::TextWrapped("%s", selected_entry->serial.c_str());
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + title_padding_below_y);

			// file tile
			ImGui::TextWrapped("%s", SmallString::from_format(FSUI_FSTR("File: {}"), Path::GetFileName(selected_entry->path)).c_str());

			// crc
			ImGui::TextUnformatted(TinyString::from_format(FSUI_FSTR("CRC: {:08X}"), selected_entry->crc));

			// region
			{
				std::string flag_texture(fmt::format("icons/flags/{}.svg", GameList::RegionToFlagFilename(selected_entry->region)));
				ImGui::TextUnformatted(FSUI_CSTR("Region: "));
				ImGui::SameLine();
				DrawCachedSvgTextureAsync(flag_texture, LayoutScale(23.0f, 16.0f), SvgScaling::Fit);
				ImGui::SameLine();
				ImGui::Text(" (%s)", GameList::RegionToString(selected_entry->region, true));
			}

			// compatibility
			ImGui::TextUnformatted(FSUI_CSTR("Compatibility: "));
			ImGui::SameLine();
			if (selected_entry->compatibility_rating != GameDatabaseSchema::Compatibility::Unknown)
			{
				DrawSvgTexture(s_game_compatibility_textures[static_cast<u32>(selected_entry->compatibility_rating) - 1].get(), LayoutScale(64.0f, 16.0f));
				ImGui::SameLine();
			}
			ImGui::Text(" (%s)", GameList::EntryCompatibilityRatingToString(selected_entry->compatibility_rating, true));

			// play time
			ImGui::TextUnformatted(
				SmallString::from_format(FSUI_FSTR("Time Played: {}"), GameList::FormatTimespan(selected_entry->total_played_time)));
			ImGui::TextUnformatted(
				SmallString::from_format(FSUI_FSTR("Last Played: {}"), GameList::FormatTimestamp(selected_entry->last_played_time)));

			// size
			ImGui::TextUnformatted(
				SmallString::from_format(FSUI_FSTR("Size: {:.2f} MB"), static_cast<float>(selected_entry->total_size) / 1048576.0f));

			ImGui::PopFont();
		}
		else
		{
			// title
			const char* title = FSUI_CSTR("No Game Selected");
			ImGui::PushFont(g_large_font.first, g_large_font.second);
			text_width = ImGui::CalcTextSize(title, nullptr, false, work_width).x;
			ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
			ImGui::TextWrapped("%s", title);
			ImGui::PopFont();
		}

		ImGui::EndGroup();
		ImGui::PopTextWrapPos();
		ImGui::PopStyleVar();
		PopPrimaryColor();
	}
	EndFullscreenColumnWindow();
	EndFullscreenColumns();

	ImGui::PopStyleColor();
}

void FullscreenUI::DrawGameGrid(const ImVec2& heading_size)
{
	ImGuiIO& io = ImGui::GetIO();
	if (!BeginFullscreenWindow(
			ImVec2(0.0f, heading_size.y),
			ImVec2(io.DisplaySize.x, io.DisplaySize.y - heading_size.y - LayoutScale(LAYOUT_FOOTER_HEIGHT)), "game_grid",
			UIBackgroundColor))
	{
		EndFullscreenWindow();
		return;
	}

	if (!AreAnyDialogsOpen() && WantsToCloseMenu())
		SwitchToLanding();

	ResetFocusHere();
	BeginMenuButtons();

	const ImGuiStyle& style = ImGui::GetStyle();

	const float title_spacing = LayoutScale(10.0f);
	const float item_spacing = LayoutScale(20.0f);
	const float item_width_with_spacing = std::floor(LayoutScale(LAYOUT_SCREEN_WIDTH / 5.0f));
	const float item_width = item_width_with_spacing - item_spacing;
	const float image_width = item_width - (style.FramePadding.x * 2.0f);
	const float image_height = image_width * 1.33f;
	const ImVec2 image_size(image_width, image_height);
	const float item_height = (style.FramePadding.y * 2.0f) + image_height + title_spacing + g_medium_font.second;
	const ImVec2 item_size(item_width, item_height);
	const u32 grid_count_x = std::floor(ImGui::GetWindowWidth() / item_width_with_spacing);
	const float start_x =
		(static_cast<float>(ImGui::GetWindowWidth()) - (item_width_with_spacing * static_cast<float>(grid_count_x))) * 0.5f;

	SmallString draw_title;

	u32 grid_x = 0;
	for (const GameList::Entry* entry : s_game_list_sorted_entries)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			continue;

		if (grid_x == grid_count_x)
		{
			grid_x = 0;
			ImGui::SetCursorPosX(start_x);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + item_spacing);
		}
		else
		{
			ImGui::SameLine(start_x + static_cast<float>(grid_x) * (item_width + item_spacing));
		}

		const ImGuiID id = window->GetID(entry->path.c_str(), entry->path.c_str() + entry->path.length());
		const ImVec2 pos(window->DC.CursorPos);
		ImRect bb(pos, pos + item_size);
		ImGui::ItemSize(item_size);
		if (ImGui::ItemAdd(bb, id))
		{
			bool held;
			bool hovered;
			bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);
			if (hovered)
			{
				const ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered, 0.7f);

				const float t = std::min<float>(std::abs(std::sin(ImGui::GetTime() * 0.75) * 1.1), 1.0f);
				ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_Border, t));

				ImGuiFullscreen::DrawMenuButtonFrame(bb.Min, bb.Max, col, true, LayoutScale(ImGuiFullscreen::LAYOUT_FRAME_ROUNDING));

				ImGui::PopStyleColor();
			}

			bb.Min += style.FramePadding;
			bb.Max -= style.FramePadding;

			DrawGameCover(entry, ImGui::GetWindowDrawList(), bb.Min, bb.Min + image_size);

			const bool show_titles = Host::GetBaseBoolSettingValue("UI", "FullscreenUIShowGameGridTitles", true);

			if (show_titles)
			{
				const ImRect title_bb(ImVec2(bb.Min.x, bb.Min.y + image_height + title_spacing), bb.Max);
				const std::string_view full_title(entry->GetTitle(s_prefer_english_titles));
				const std::string_view title = TrimString(g_medium_font, full_title, title_bb.GetWidth());
				draw_title.clear();
				fmt::format_to(std::back_inserter(draw_title), "{}{}", title, (title.length() == full_title.length()) ? "" : g_ellipsis);
				ImGui::PushFont(g_medium_font.first, g_medium_font.second);
				ImGuiFullscreen::RenderTextClippedWithShadow(
					title_bb.Min, title_bb.Max, draw_title.c_str(), draw_title.c_str() + draw_title.length(), nullptr, ImVec2(0.5f, 0.0f), &title_bb);
				ImGui::PopFont();
			}

			if (pressed)
			{
				HandleGameListActivate(entry);
			}
			else if (hovered && (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::Shortcut(ImGuiKey_NavGamepadMenu) ||
									ImGui::Shortcut(ImGuiKey_F3)))
			{
				HandleGameListOptions(entry);
			}
		}

		grid_x++;
	}

	EndMenuButtons();
	EndFullscreenWindow();
}

void FullscreenUI::HandleGameListActivate(const GameList::Entry* entry)
{
	// launch game
	if (!OpenLoadStateSelectorForGameResume(entry))
		DoStartPath(entry->path);
}

void FullscreenUI::HandleGameListOptions(const GameList::Entry* entry)
{
	ImGuiFullscreen::ChoiceDialogOptions options = {
		{FSUI_ICONSTR(ICON_FA_WRENCH, "Game Properties"), false},
		{FSUI_ICONSTR(ICON_FA_PLAY, "Resume Game"), false},
		{FSUI_ICONSTR(ICON_FA_ARROW_ROTATE_LEFT, "Load State"), false},
		{FSUI_ICONSTR(ICON_PF_STAR, "Default Boot"), false},
		{FSUI_ICONSTR(ICON_FA_FORWARD_FAST, "Fast Boot"), false},
		{FSUI_ICONSTR(ICON_FA_COMPACT_DISC, "Full Boot"), false},
	};

	const time_t entry_played_time = GameList::GetCachedPlayedTimeForSerial(entry->serial);
	if (entry_played_time)
		options.emplace_back(FSUI_ICONSTR(ICON_FA_STOPWATCH, "Reset Play Time"), false);
	options.emplace_back(FSUI_ICONSTR(ICON_FA_SQUARE_XMARK, "Close Menu"), false);

	const bool has_resume_state = VMManager::HasSaveStateInSlot(entry->serial.c_str(), entry->crc, -1);
	OpenChoiceDialog(entry->GetTitle(s_prefer_english_titles).c_str(), false, std::move(options),
		[has_resume_state, entry_path = entry->path, entry_serial = entry->serial, entry_title = entry->title, entry_played_time]
		(s32 index, const std::string& title, bool checked) {
			switch (index)
			{
				case 0: // Open Game Properties
					SwitchToGameSettings(entry_path);
					break;
				case 1: // Resume Game
					DoStartPath(entry_path, has_resume_state ? std::optional<s32>(-1) : std::optional<s32>());
					break;
				case 2: // Load State
					OpenLoadStateSelectorForGame(entry_path);
					break;
				case 3: // Default Boot
					DoStartPath(entry_path);
					break;
				case 4: // Fast Boot
					DoStartPath(entry_path, std::nullopt, true);
					break;
				case 5: // Full Boot
					DoStartPath(entry_path, std::nullopt, false);
					break;
				case 6:
					{
						// Close Menu
						if (!entry_played_time)
							break;

						// Reset Play Time
						OpenConfirmMessageDialog(FSUI_ICONSTR(ICON_FA_STOPWATCH, "Confirm Reset"),
							fmt::format(FSUI_FSTR("Are you sure you want to reset the play time for '{}' ({})?\n\n"
												  "Your current play time is {}.\n\nThis action cannot be undone."),
											entry_title.empty() ? FSUI_STR("empty title") : entry_title,
											entry_serial.empty() ? FSUI_STR("no serial") : entry_serial,
											GameList::FormatTimespan(entry_played_time, true)),
							[entry_serial](bool result) {
								if (result)
									GameList::ClearPlayedTimeForSerial(entry_serial);
							}, false);
					}
					break;
				default: // Close Menu
					break;
			}

			CloseChoiceDialog();
		});
}

void FullscreenUI::DrawGameListSettingsWindow()
{
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 heading_size =
		ImVec2(io.DisplaySize.x, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY) +
									 (LayoutScale(LAYOUT_MENU_BUTTON_Y_PADDING) * 2.0f) + LayoutScale(2.0f));

	const float bg_alpha = VMManager::HasValidVM() ? 0.90f : 1.0f;

	if (BeginFullscreenWindow(ImVec2(0.0f, 0.0f), heading_size, "gamelist_view", MulAlpha(UIPrimaryColor, bg_alpha)))
	{
		BeginNavBar();

		if (NavButton(ICON_PF_BACKWARD, true, true))
		{
			s_current_main_window = MainWindowType::GameList;
			QueueResetFocus(FocusResetType::WindowChanged);
		}

		NavTitle(FSUI_CSTR("Game List Settings"));
		EndNavBar();
	}

	EndFullscreenWindow();

	if (!BeginFullscreenWindow(
			ImVec2(0.0f, heading_size.y),
			ImVec2(io.DisplaySize.x, io.DisplaySize.y - heading_size.y - LayoutScale(LAYOUT_FOOTER_HEIGHT)),
			"settings_parent", UIBackgroundColor, 0.0f, ImVec2(ImGuiFullscreen::LAYOUT_MENU_WINDOW_X_PADDING, 0.0f)))
	{
		EndFullscreenWindow();
		return;
	}

	if (ImGui::IsWindowFocused() && WantsToCloseMenu())
	{
		s_current_main_window = MainWindowType::GameList;
		QueueResetFocus(FocusResetType::WindowChanged);
	}

	auto lock = Host::GetSettingsLock();
	SettingsInterface* bsi = GetEditingSettingsInterface(false);

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("Search Directories"));
	if (MenuButton(FSUI_ICONSTR(ICON_FA_FOLDER_PLUS, "Add Search Directory"), FSUI_CSTR("Adds a new directory to the game search list.")))
	{
		OpenFileSelector(FSUI_ICONSTR(ICON_FA_FOLDER_PLUS, "Add Search Directory"), true, [](const std::string& dir) {
			if (!dir.empty())
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();

				bsi->AddToStringList("GameList", "RecursivePaths", dir.c_str());
				bsi->RemoveFromStringList("GameList", "Paths", dir.c_str());
				SetSettingsChanged(bsi);
				PopulateGameListDirectoryCache(bsi);
				Host::RefreshGameListAsync(false);
			}

			CloseFileSelector();
		});
	}

	for (const auto& it : s_game_list_directories_cache)
	{
		if (MenuButton(it.first.c_str(), it.second ? FSUI_CSTR("Scanning Subdirectories") : FSUI_CSTR("Not Scanning Subdirectories")))
		{
			ImGuiFullscreen::ChoiceDialogOptions options = {
				{FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Open in File Browser"), false},
				{it.second ? (FSUI_ICONSTR(ICON_FA_FOLDER_MINUS, "Disable Subdirectory Scanning")) :
							 (FSUI_ICONSTR(ICON_FA_FOLDER_PLUS, "Enable Subdirectory Scanning")),
					false},
				{FSUI_ICONSTR(ICON_FA_TRASH, "Remove From List"), false},
				{FSUI_ICONSTR(ICON_FA_SQUARE_XMARK, "Close Menu"), false},
			};

			OpenChoiceDialog(SmallString::from_format(ICON_FA_FOLDER " {}", it.first).c_str(), false, std::move(options),
				[dir = it.first, recursive = it.second](s32 index, const std::string& title, bool checked) {
					if (index < 0)
						return;

					if (index == 0)
					{
						// Open In File Browser.
						ExitFullscreenAndOpenURL(Path::CreateFileURL(dir));
					}
					else if (index == 1)
					{
						// Toggle Subdirectory Scanning.
						{
							auto lock = Host::GetSettingsLock();
							SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
							if (!recursive)
							{
								bsi->RemoveFromStringList("GameList", "Paths", dir.c_str());
								bsi->AddToStringList("GameList", "RecursivePaths", dir.c_str());
							}
							else
							{
								bsi->RemoveFromStringList("GameList", "RecursivePaths", dir.c_str());
								bsi->AddToStringList("GameList", "Paths", dir.c_str());
							}

							SetSettingsChanged(bsi);
							PopulateGameListDirectoryCache(bsi);
						}

						Host::RefreshGameListAsync(false);
					}
					else if (index == 2)
					{
						// Remove From List.
						auto lock = Host::GetSettingsLock();
						SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
						bsi->RemoveFromStringList("GameList", "Paths", dir.c_str());
						bsi->RemoveFromStringList("GameList", "RecursivePaths", dir.c_str());
						SetSettingsChanged(bsi);
						PopulateGameListDirectoryCache(bsi);
						Host::RefreshGameListAsync(false);
					}

					CloseChoiceDialog();
				});
		}
	}

	static constexpr const char* view_types[] = {
		FSUI_NSTR("Game Grid"),
		FSUI_NSTR("Game List"),
	};
	static constexpr const char* sort_types[] = {
		FSUI_NSTR("Type"),
		FSUI_NSTR("Serial"),
		FSUI_NSTR("Title"),
		FSUI_NSTR("File Title"),
		FSUI_NSTR("CRC"),
		FSUI_NSTR("Time Played"),
		FSUI_NSTR("Last Played"),
		FSUI_NSTR("Size"),
	};

	MenuHeading(FSUI_CSTR("List Settings"));
	{
		DrawIntListSetting(bsi, FSUI_ICONSTR(ICON_FA_BORDER_ALL, "Default View"), FSUI_CSTR("Sets which view the game list will open to."),
			"UI", "DefaultFullscreenUIGameView", 0, view_types, std::size(view_types), true);
		DrawIntListSetting(bsi, FSUI_ICONSTR(ICON_FA_SORT, "Sort By"), FSUI_CSTR("Determines which field the game list will be sorted by."),
			"UI", "FullscreenUIGameSort", 0, sort_types, std::size(sort_types), true);
		DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_ARROW_DOWN_A_Z, "Sort Reversed"),
			FSUI_CSTR("Reverses the game list sort order from the default (usually ascending to descending)."), "UI",
			"FullscreenUIGameSortReverse", false);
		DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_TAG, "Show Titles"),
			FSUI_CSTR("Shows Titles for Games when in Game Grid View Mode"), "UI",
			"FullscreenUIShowGameGridTitles", true);
	}

	MenuHeading(FSUI_CSTR("Cover Settings"));
	{
		DrawFolderSetting(bsi, FSUI_ICONSTR(ICON_FA_FOLDER, "Covers Directory"), "Folders", "Covers", EmuFolders::Covers);
		if (MenuButton(
				FSUI_ICONSTR(ICON_FA_DOWNLOAD, "Download Covers"), FSUI_CSTR("Downloads covers from a user-specified URL template.")))
		{
			Host::OnCoverDownloaderOpenRequested();
		}
	}

	MenuHeading(FSUI_CSTR("Operations"));
	{
		if (MenuButton(
				FSUI_ICONSTR(ICON_FA_MAGNIFYING_GLASS, "Scan For New Games"), FSUI_CSTR("Identifies any new files added to the game directories.")))
		{
			Host::RefreshGameListAsync(false);
		}
		if (MenuButton(FSUI_ICONSTR(ICON_FA_ARROW_ROTATE_RIGHT, "Rescan All Games"),
				FSUI_CSTR("Forces a full rescan of all games previously identified.")))
		{
			Host::RefreshGameListAsync(true);
		}
	}

	EndMenuButtons();

	EndFullscreenWindow();

	SetStandardSelectionFooterText(true);
}

void FullscreenUI::SwitchToGameList()
{
	s_current_main_window = MainWindowType::GameList;
	s_game_list_view = static_cast<GameListView>(Host::GetBaseIntSettingValue("UI", "DefaultFullscreenUIGameView", 0));
	{
		auto lock = Host::GetSettingsLock();
		PopulateGameListDirectoryCache(Host::Internal::GetBaseSettingsLayer());
	}
	QueueResetFocus(FocusResetType::WindowChanged);
}

GSTexture* FullscreenUI::GetGameListCover(const GameList::Entry* entry)
{
	// lookup and grab cover image
	auto cover_it = s_cover_image_map.find(entry->path);
	if (cover_it == s_cover_image_map.end())
	{
		std::string cover_path(GameList::GetCoverImagePathForEntry(entry));
		cover_it = s_cover_image_map.emplace(entry->path, std::move(cover_path)).first;
	}

	return (!cover_it->second.empty()) ? GetCachedTextureAsync(cover_it->second.c_str()) : nullptr;
}

GSTexture* FullscreenUI::GetTextureForGameListEntryType(GameList::EntryType type, const ImVec2& size, SvgScaling mode)
{
	switch (type)
	{
		case GameList::EntryType::ELF:
			return GetCachedSvgTexture("fullscreenui/applications-system.svg", size, mode);

		case GameList::EntryType::PS1Disc:
		case GameList::EntryType::PS2Disc:
		default:
			return GetCachedSvgTexture("fullscreenui/media-cdrom.svg", size, mode);
	}
}

void FullscreenUI::DrawGameCover(const GameList::Entry* entry, const ImVec2& size)
{
	// Used in DrawGameList (selected preview)
	const GSTexture* cover_texture = GetGameListCover(entry);

	pxAssert(ImGui::GetCurrentContext()->Style.ImageBorderSize == 0);
	const ImVec2 origin = ImGui::GetCursorPos();

	if (cover_texture)
	{
		const ImRect image_rect(CenterImage(size,
			ImVec2(static_cast<float>(cover_texture->GetWidth()), static_cast<float>(cover_texture->GetHeight()))));

		ImGui::SetCursorPos(origin + image_rect.Min);
		ImGui::Image(reinterpret_cast<ImTextureID>(cover_texture->GetNativeHandle()), image_rect.GetSize());
	}
	else
	{
		const float min_size = std::min(size.x, size.y);
		const ImVec2 image_square(min_size, min_size);
		GSTexture* const icon_texture = GetTextureForGameListEntryType(entry->type, image_square);

		const ImRect image_rect(CenterImage(size, image_square));

		ImGui::SetCursorPos(origin + image_rect.Min);
		DrawSvgTexture(icon_texture, image_square);
	}
	// Pretend the image we drew was the the size passed to us
	ImGui::SetCursorPos(origin);
	ImGui::Dummy(size);
}

void FullscreenUI::DrawGameCover(const GameList::Entry* entry, ImDrawList* draw_list, const ImVec2& min, const ImVec2& max)
{
	// Used in DrawPauseMenu, DrawGameList (list item), DrawGameGrid
	const GSTexture* cover_texture = GetGameListCover(entry);

	if (cover_texture)
	{
		const ImRect image_rect(CenterImage(ImRect(min, max),
			ImVec2(static_cast<float>(cover_texture->GetWidth()), static_cast<float>(cover_texture->GetHeight()))));

		draw_list->AddImage(reinterpret_cast<ImTextureID>(cover_texture->GetNativeHandle()),
			image_rect.Min, image_rect.Max);
	}
	else
	{
		const float min_size = std::min(max.x - min.x, max.y - min.y);
		const ImVec2 image_square(min_size, min_size);

		const ImRect image_rect(CenterImage(ImRect(min, max), image_square));

		DrawListSvgTexture(draw_list, GetTextureForGameListEntryType(entry->type, image_square, SvgScaling::Fit),
			image_rect.Min, image_rect.Max);
	}
}

void FullscreenUI::DrawFallbackCover(const ImVec2& size)
{
	pxAssert(ImGui::GetCurrentContext()->Style.ImageBorderSize == 0);
	const ImVec2 origin = ImGui::GetCursorPos();

	const float min_size = std::min(size.x, size.y);
	const ImVec2 image_square(min_size, min_size);
	GSTexture* const icon_texture = GetTextureForGameListEntryType(GameList::EntryType::PS2Disc, image_square);

	const ImRect image_rect(CenterImage(size, image_square));

	ImGui::SetCursorPos(origin + image_rect.Min);
	DrawSvgTexture(icon_texture, image_square);

	// Pretend the image we drew was the the size passed to us
	ImGui::SetCursorPos(origin);
	ImGui::Dummy(size);
}

void FullscreenUI::DrawFallbackCover(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max)
{
	const float min_size = std::min(max.x - min.x, max.y - min.y);
	const ImVec2 image_square(min_size, min_size);

	const ImRect image_rect(CenterImage(ImRect(min, max), image_square));

	DrawListSvgTexture(draw_list, GetTextureForGameListEntryType(GameList::EntryType::PS2Disc, image_square, SvgScaling::Fit),
		image_rect.Min, image_rect.Max);
}

//////////////////////////////////////////////////////////////////////////
// Overlays
//////////////////////////////////////////////////////////////////////////

void FullscreenUI::ExitFullscreenAndOpenURL(const std::string_view url)
{
	Host::RunOnCPUThread([url = std::string(url)]() {
		if (Host::IsFullscreen())
			Host::SetFullscreen(false);

		Host::OpenURL(url);
	});
}

void FullscreenUI::CopyTextToClipboard(std::string title, const std::string_view text)
{
	if (Host::CopyTextToClipboard(text))
		ShowToast(std::string(), std::move(title));
	else
		ShowToast(std::string(), FSUI_STR("Failed to copy text to clipboard."));
}

void FullscreenUI::OpenAboutWindow()
{
	s_about_window_open = true;
}

void FullscreenUI::DrawAboutWindow()
{
	ImGui::SetNextWindowSize(LayoutScale(1000.0f, 600.0f));
	ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::OpenPopup(FSUI_CSTR("About PCSX2"));

	ImGui::PushFont(g_large_font.first, g_large_font.second);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(ImGuiFullscreen::LAYOUT_WINDOW_ROUNDING));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(30.0f, 30.0f));

	if (ImGui::BeginPopupModal(FSUI_CSTR("About PCSX2"), &s_about_window_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
	{
		const ImVec2 image_size = LayoutScale(500.0f, 76.0f);
		const ImRect image_bb(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImVec2(ImGui::GetCurrentWindow()->WorkRect.GetWidth(), image_size.y));
		const ImRect image_rect(CenterImage(image_bb, image_size));
		const float start_y = ImGui::GetCursorPosY();

		DrawListSvgTexture(ImGui::GetWindowDrawList(), s_banner_texture.get(), image_rect.Min, image_rect.Max);

		ImGui::SetCursorPosY(start_y + image_size.y + LayoutScale(2.0f));
		static const std::string version_text = fmt::format(FSUI_FSTR("Version: {}"), BuildVersion::GitRev);
		const float version_center_x =
			ImGui::GetCursorPosX() + ((ImGui::GetCurrentWindow()->WorkRect.GetWidth() - ImGui::CalcTextSize(version_text.c_str()).x) * 0.5f);
		ImGui::SetCursorPosX(version_center_x);
		ImGui::TextUnformatted(version_text.c_str());

		const float indent = LayoutScale(12.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + indent);
		ImGui::TextWrapped("%s", FSUI_CSTR(
									 "PCSX2 is a free and open-source PlayStation 2 (PS2) emulator. Its purpose is to emulate the PS2's hardware, using a "
									 "combination of MIPS CPU Interpreters, Recompilers and a Virtual Machine which manages hardware states and PS2 system memory. "
									 "This allows you to play PS2 games on your PC, with many additional features and benefits."));
		ImGui::NewLine();

		ImGui::TextWrapped("%s",
			FSUI_CSTR("PlayStation 2 and PS2 are registered trademarks of Sony Interactive Entertainment. This application is not "
					  "affiliated in any way with Sony Interactive Entertainment."));

		BeginMenuButtons();

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_GLOBE, "Website"), false))
			ExitFullscreenAndOpenURL(PCSX2_WEBSITE_URL);

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_PERSON_BOOTH, "Support Forums"), false))
			ExitFullscreenAndOpenURL(PCSX2_FORUMS_URL);

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_BUG, "GitHub Repository"), false))
			ExitFullscreenAndOpenURL(PCSX2_GITHUB_URL);

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_NEWSPAPER, "License"), false))
			ExitFullscreenAndOpenURL(PCSX2_LICENSE_URL);

		if (ActiveButton(FSUI_ICONSTR(ICON_FA_SQUARE_XMARK, "Close"), false) || WantsToCloseMenu())
		{
			ImGui::CloseCurrentPopup();
			s_about_window_open = false;
		}
		else
		{
			SetStandardSelectionFooterText(true);
		}

		EndMenuButtons();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
	ImGui::PopFont();
}

bool FullscreenUI::OpenAchievementsWindow()
{
	if (!VMManager::HasValidVM() || !Achievements::IsActive())
		return false;

	MTGS::RunOnGSThread([]() {
		if (!ImGuiManager::InitializeFullscreenUI())
			return;

		SwitchToAchievementsWindow();
	});

	return true;
}

void FullscreenUI::DrawAchievementsLoginWindow()
{
	if (s_achievements_login_open && !ImGui::IsPopupOpen("RetroAchievements"))
		ImGui::OpenPopup("RetroAchievements");

	const float dialog_width = std::clamp(LayoutScale(420.0f), 300.0f, ImGui::GetIO().DisplaySize.x);
	ImGui::SetNextWindowSizeConstraints(ImVec2(dialog_width, 0.0f), ImVec2(dialog_width, ImGui::GetIO().DisplaySize.y));
	ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(ImGuiFullscreen::LAYOUT_WINDOW_ROUNDING));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(24.0f, 24.0f));
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.13f, 0.13f, 0.13f, 0.95f));

	if (ImGui::BeginPopupModal("RetroAchievements", &s_achievements_login_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
	{
		const float content_width = ImGui::GetContentRegionAvail().x;

		ImGui::PushFont(g_large_font.first, g_large_font.second);

		const float icon_height = LayoutScale(24.0f);
		const float icon_width = icon_height * (500.0f / 275.0f);
		GSTexture* ra_icon = GetCachedSvgTextureAsync("icons/ra-icon.svg", ImVec2(icon_width, icon_height));
		const float title_width = ImGui::CalcTextSize("RetroAchievements").x;
		const float header_width = (ra_icon ? icon_width + LayoutScale(10.0f) : 0.0f) + title_width;
		const float header_start = (content_width - header_width) * 0.5f;

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + header_start);

		if (ra_icon)
		{
			ImGui::Image(reinterpret_cast<ImTextureID>(ra_icon->GetNativeHandle()),
				ImVec2(icon_width, icon_height));
			ImGui::SameLine();
		}

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + LayoutScale(1.0f));
		ImGui::TextUnformatted(FSUI_CSTR("RetroAchievements"));
		ImGui::PopFont();

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::PushTextWrapPos(content_width);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
		ImGui::TextWrapped("%s", FSUI_CSTR("Please enter your user name and password for retroachievements.org below.\n\nYour password will not be saved in PCSX2, an access token will be generated and used instead."));
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, LayoutScale(ImGuiFullscreen::LAYOUT_FRAME_ROUNDING));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, LayoutScale(12.0f, 10.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, LayoutScale(1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

		if (s_achievements_login_logging_in || s_achievements_login_show_dismiss)
			ImGui::BeginDisabled();

		ImGui::SetNextItemWidth(content_width);
		ImGui::InputTextWithHint("##username", FSUI_CSTR("Username"), s_achievements_login_username, sizeof(s_achievements_login_username));

		ImGui::Spacing();

		ImGui::SetNextItemWidth(content_width);
		ImGui::InputTextWithHint("##password", FSUI_CSTR("Password"), s_achievements_login_password, sizeof(s_achievements_login_password), ImGuiInputTextFlags_Password);

		ImGui::PopStyleColor(5);
		ImGui::PopStyleVar(3);

		if (s_achievements_login_logging_in || s_achievements_login_show_dismiss)
			ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Spacing();

		if (s_achievements_login_logging_in)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
			const float status_width = ImGui::CalcTextSize("Logging in...").x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (content_width - status_width) * 0.5f);
			ImGui::TextUnformatted(FSUI_CSTR("Logging in..."));
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}
		else if (s_achievements_login_show_dismiss)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.9f, 0.35f, 1.0f));
			const TinyString username = Host::GetBaseTinyStringSettingValue("Achievements", "Username", "");
			const SmallString success_text = SmallString::from_format(
				FSUI_FSTR("Successfully logged in as {}."), username.empty() ? "Unknown" : username.view());
			const float status_width = ImGui::CalcTextSize(success_text.c_str()).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (content_width - status_width) * 0.5f);
			ImGui::TextUnformatted(success_text.c_str());
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		const float button_height = std::max(LayoutScale(36.0f), 28.0f);
		const float button_spacing = LayoutScale(12.0f);
		const float button_width = (content_width - button_spacing) * 0.5f;

		auto CloseLoginPopup = []() {
			ImGui::CloseCurrentPopup();
			s_achievements_login_open = false;
			s_achievements_login_logging_in = false;
			s_achievements_login_show_dismiss = false;

			s_achievements_login_username[0] = '\0';
			s_achievements_login_password[0] = '\0';
		};

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, LayoutScale(ImGuiFullscreen::LAYOUT_FRAME_ROUNDING));

		const bool can_login = !s_achievements_login_show_dismiss && !s_achievements_login_logging_in &&
		                       strlen(s_achievements_login_username) > 0 &&
		                       strlen(s_achievements_login_password) > 0;

		if (s_achievements_login_show_dismiss)
		{
			// keep dialog open and let user explicitly dismiss.
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.8f, 1.0f));

			if (ImGui::Button(FSUI_CSTR("Dismiss"), ImVec2(button_width, button_height)) && !s_achievements_login_logging_in)
				CloseLoginPopup();

			ImGui::PopStyleColor(3);

			ImGui::SameLine(0, button_spacing);

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
			ImGui::BeginDisabled();
			ImGui::Button(FSUI_CSTR("Login"), ImVec2(button_width, button_height));
			ImGui::EndDisabled();
			ImGui::PopStyleColor(3);

			ImGui::PopStyleVar();
			ImGui::EndPopup();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);
			return;
		}

		if (can_login)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.8f, 1.0f));
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		}

		if (ImGui::Button(FSUI_CSTR("Login"), ImVec2(button_width, button_height)) && can_login)
		{
			s_achievements_login_logging_in = true;
			s_achievements_login_show_dismiss = false;

			Host::RunOnCPUThread([username = std::string(s_achievements_login_username),
									 password = std::string(s_achievements_login_password)]() {
				Error error;
				const bool result = Achievements::Login(username.c_str(), password.c_str(), &error);

				s_achievements_login_logging_in = false;

				if (!result)
				{
					ShowToast(std::string(), fmt::format(FSUI_FSTR("Login failed.\nError: {}\n\nPlease check your username and password, and try again."),
												 error.GetDescription()));
					return;
				}

				s_achievements_login_password[0] = '\0';

				if (s_achievements_login_reason == Achievements::LoginRequestReason::UserInitiated)
				{
					if (!Host::GetBaseBoolSettingValue("Achievements", "Enabled", false))
					{
						OpenConfirmMessageDialog(FSUI_STR("Enable Achievements"),
							FSUI_STR("Achievement tracking is not currently enabled. Your login will have no effect until "
									 "after tracking is enabled.\n\nDo you want to enable tracking now?"),
							[](bool result) {
								if (result)
								{
									Host::SetBaseBoolSettingValue("Achievements", "Enabled", true);
									Host::CommitBaseSettingChanges();
									VMManager::ApplySettings();
								}
							});
					}

					if (!Host::GetBaseBoolSettingValue("Achievements", "ChallengeMode", false))
					{
						OpenConfirmMessageDialog(FSUI_STR("Enable Hardcore Mode"),
							FSUI_STR("Hardcore mode is not currently enabled. Enabling hardcore mode allows you to set times, scores, and "
									 "participate in game-specific leaderboards.\n\nHowever, hardcore mode also prevents the usage of save "
									 "states, cheats and slowdown functionality.\n\nDo you want to enable hardcore mode?"),
							[](bool result) {
								if (result)
								{
									Host::SetBaseBoolSettingValue("Achievements", "ChallengeMode", true);
									Host::CommitBaseSettingChanges();
									VMManager::ApplySettings();

									bool has_active_game;
									{
										auto lock = Achievements::GetLock();
										has_active_game = Achievements::HasActiveGame();
									}

									if (has_active_game)
									{
										OpenConfirmMessageDialog(FSUI_STR("Reset System"),
											FSUI_STR("Hardcore mode will not be enabled until the system is reset. Do you want to reset the system now?"),
											[](bool reset) {
												if (reset && VMManager::HasValidVM())
													RequestReset();
											});
									}
								}
							});
					}
				}

				s_achievements_login_show_dismiss = true;
			});
		}

		ImGui::PopStyleColor(3);
		ImGui::SameLine(0, button_spacing);

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

		if (ImGui::Button(FSUI_CSTR("Cancel"), ImVec2(button_width, button_height)) && !s_achievements_login_logging_in)
		{
			if (s_achievements_login_reason == Achievements::LoginRequestReason::TokenInvalid)
			{
				if (VMManager::HasValidVM() && !Achievements::HasActiveGame())
					Achievements::DisableHardcoreMode();
			}

			CloseLoginPopup();
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();

		ImGui::EndPopup();
	}

	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

bool FullscreenUI::IsAchievementsWindowOpen()
{
	return (s_current_main_window == MainWindowType::Achievements);
}

void FullscreenUI::SwitchToAchievementsWindow()
{
	if (!VMManager::HasValidVM())
		return;

	if (!Achievements::HasAchievements())
	{
		ShowToast(std::string(), FSUI_STR("This game has no achievements."));
		return;
	}

	if (!Achievements::PrepareAchievementsWindow())
		return;

	if (s_current_main_window != MainWindowType::PauseMenu)
	{
		PauseForMenuOpen(false);
		ForceKeyNavEnabled();
	}

	s_current_main_window = MainWindowType::Achievements;
	QueueResetFocus(FocusResetType::WindowChanged);
}

bool FullscreenUI::OpenLeaderboardsWindow()
{
	if (!VMManager::HasValidVM() || !Achievements::IsActive())
		return false;

	MTGS::RunOnGSThread([]() {
		if (!ImGuiManager::InitializeFullscreenUI())
			return;

		SwitchToLeaderboardsWindow();
	});

	return true;
}

bool FullscreenUI::IsLeaderboardsWindowOpen()
{
	return (s_current_main_window == MainWindowType::Leaderboards);
}

void FullscreenUI::SwitchToLeaderboardsWindow()
{
	if (!VMManager::HasValidVM())
		return;

	if (!Achievements::HasLeaderboards())
	{
		ShowToast(std::string(), FSUI_STR("This game has no leaderboards."));
		return;
	}

	if (!Achievements::PrepareLeaderboardsWindow())
		return;

	if (s_current_main_window != MainWindowType::PauseMenu)
	{
		PauseForMenuOpen(false);
		ForceKeyNavEnabled();
	}

	s_current_main_window = MainWindowType::Leaderboards;
	QueueResetFocus(FocusResetType::WindowChanged);
}

void FullscreenUI::DrawAchievementsSettingsPage(std::unique_lock<std::mutex>& settings_lock)
{
#ifdef ENABLE_RAINTEGRATION
	if (Achievements::IsUsingRAIntegration())
	{
		BeginMenuButtons();
		ActiveButton(FSUI_ICONSTR(ICON_FA_BAN, "RAIntegration is being used instead of the built-in achievements implementation."), false,
			false, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
		EndMenuButtons();
		return;
	}
#endif

	SettingsInterface* bsi = GetEditingSettingsInterface();
	bool check_challenge_state = false;

	BeginMenuButtons();

	MenuHeading(FSUI_CSTR("Settings"));
	check_challenge_state = DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_TROPHY, "Enable Achievements"),
		FSUI_CSTR("When enabled and logged in, PCSX2 will scan for achievements on startup."), "Achievements", "Enabled", false);

	const bool enabled = bsi->GetBoolValue("Achievements", "Enabled", false);

	check_challenge_state |= DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_PF_DUMBELL, "Hardcore Mode"),
		FSUI_CSTR(
			"\"Challenge\" mode for achievements, including leaderboard tracking. Disables save state, cheats, and slowdown functions."),
		"Achievements", "ChallengeMode", false, enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_BELL, "Achievement Notifications"),
		FSUI_CSTR("Displays popup messages on events such as achievement unlocks and leaderboard submissions."), "Achievements",
		"Notifications", true, enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_LIST_OL, "Leaderboard Notifications"),
		FSUI_CSTR("Displays popup messages when starting, submitting, or failing a leaderboard challenge."), "Achievements",
		"LeaderboardNotifications", true, enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_HEADPHONES, "Sound Effects"),
		FSUI_CSTR("Plays sound effects for events such as achievement unlocks and leaderboard submissions."), "Achievements",
		"SoundEffects", true, enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_PF_HEARTBEAT_ALT, "Enable In-Game Overlays"),
		FSUI_CSTR("Shows icons in the screen when a challenge/primed achievement is active."), "Achievements",
		"Overlays", true, enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_PF_HEARTBEAT_ALT, "Enable In-Game Leaderboard Overlays"),
		FSUI_CSTR("Shows icons in the screen when leaderboard tracking is active."), "Achievements",
		"LBOverlays", true, enabled);

	if (enabled)
	{
		const char* alignment_options[] = {
			TRANSLATE_NOOP("FullscreenUI", "Top Left"),
			TRANSLATE_NOOP("FullscreenUI", "Top Center"),
			TRANSLATE_NOOP("FullscreenUI", "Top Right"),
			TRANSLATE_NOOP("FullscreenUI", "Center Left"),
			TRANSLATE_NOOP("FullscreenUI", "Center"),
			TRANSLATE_NOOP("FullscreenUI", "Center Right"),
			TRANSLATE_NOOP("FullscreenUI", "Bottom Left"),
			TRANSLATE_NOOP("FullscreenUI", "Bottom Center"),
			TRANSLATE_NOOP("FullscreenUI", "Bottom Right")
		};

		DrawIntListSetting(bsi, FSUI_ICONSTR(ICON_FA_ALIGN_CENTER, "Overlay Position"),
			FSUI_CSTR("Determines where achievement/leaderboard overlays are positioned on the screen."), "Achievements", "OverlayPosition",
			8, alignment_options, std::size(alignment_options), true, 0, enabled);

		const bool notifications_enabled = GetEffectiveBoolSetting(bsi, "Achievements", "Notifications", true) ||
											GetEffectiveBoolSetting(bsi, "Achievements", "LeaderboardNotifications", true);
		if (notifications_enabled)
		{
			DrawIntListSetting(bsi, FSUI_ICONSTR(ICON_FA_BELL, "Notification Position"),
				FSUI_CSTR("Determines where achievement/leaderboard notification popups are positioned on the screen."), "Achievements", "NotificationPosition",
				2, alignment_options, std::size(alignment_options), true, 0, enabled);
		}
	}
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_LOCK, "Encore Mode"),
		FSUI_CSTR("When enabled, each session will behave as if no achievements have been unlocked."), "Achievements", "EncoreMode", false,
		enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_EYE, "Spectator Mode"),
		FSUI_CSTR("When enabled, PCSX2 will assume all achievements are locked and not send any unlock notifications to the server."),
		"Achievements", "SpectatorMode", false, enabled);
	DrawToggleSetting(bsi, FSUI_ICONSTR(ICON_FA_MEDAL, "Test Unofficial Achievements"),
		FSUI_CSTR(
			"When enabled, PCSX2 will list achievements from unofficial sets. These achievements are not tracked by RetroAchievements."),
		"Achievements", "UnofficialTestMode", false, enabled);

	// Check for challenge mode just being enabled.
	if (check_challenge_state && enabled && bsi->GetBoolValue("Achievements", "ChallengeMode", false) && VMManager::HasValidVM())
	{
		// don't bother prompting if the game doesn't have achievements
		auto lock = Achievements::GetLock();
		if (Achievements::HasActiveGame() && Achievements::HasAchievementsOrLeaderboards())
		{
			ImGuiFullscreen::OpenConfirmMessageDialog(FSUI_STR("Reset System"),
				FSUI_STR("Hardcore mode will not be enabled until the system is reset. Do you want to reset the system now?"), [](bool reset) {
					if (!VMManager::HasValidVM())
						return;

					if (reset)
						RequestReset();
				});
		}
	}

	if (!IsEditingGameSettings(bsi))
	{
		MenuHeading(FSUI_CSTR("Sound Effects"));
		const auto draw_sound_setting = [bsi](const char* title, const char* key, const char* default_filename, const char* selector_title) {
			const std::string default_path = Path::Combine(EmuFolders::Resources, default_filename);
			const std::optional<SmallString> custom_path = bsi->GetOptionalSmallStringValue("Achievements", key, std::nullopt);
			const char* value = custom_path.has_value() ? custom_path->c_str() : default_path.c_str();
			if (!MenuButton(title, value))
				return;

			ImGuiFullscreen::ChoiceDialogOptions options;
			options.emplace_back(FSUI_ICONSTR(ICON_FA_FILE, "Select File"), false);
			options.emplace_back(FSUI_ICONSTR(ICON_FA_VOLUME_HIGH, "Preview"), false);
			options.emplace_back(FSUI_ICONSTR(ICON_FA_ROTATE_RIGHT, "Reset to Default"), false);
			OpenChoiceDialog(title, false, std::move(options),
				[bsi, key = std::string(key), selector_title = std::string(selector_title), default_path = std::move(default_path)](
					s32 index, const std::string&, bool) {
					if (index == 0)
					{
						auto callback = [bsi, key = key](const std::string& path) {
							if (!path.empty())
							{
								bsi->SetStringValue("Achievements", key.c_str(), path.c_str());
								SetSettingsChanged(bsi);
							}
							CloseFileSelector();
						};
						OpenFileSelector(selector_title.c_str(), false, std::move(callback), GetAudioFileFilters());
					}
					else if (index == 1)
					{
						const TinyString preview_path = bsi->GetTinyStringValue("Achievements", key.c_str(), default_path.c_str());
						if (!Common::PlaySoundAsync(preview_path.c_str()))
						{
							ShowToast(std::string(),
								fmt::format(FSUI_FSTR("Failed to preview sound:\n{}"),
									preview_path.empty() ? FSUI_STR("No file selected.") : preview_path.c_str()));
						}
					}
					else if (index == 2)
					{
						if (bsi->ContainsValue("Achievements", key.c_str()))
						{
							bsi->DeleteValue("Achievements", key.c_str());
							SetSettingsChanged(bsi);
							ShowToast(std::string(), FSUI_STR("Sound reset to default."));
						}
						else
						{
							ShowToast(std::string(), FSUI_STR("Sound is already using default."));
						}
					}
					CloseChoiceDialog();
				});
		};

		draw_sound_setting(FSUI_ICONSTR(ICON_FA_MUSIC, "Notification Sound"), "InfoSoundName", "sounds/achievements/message.wav",
			FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Select Notification Sound"));
		draw_sound_setting(FSUI_ICONSTR(ICON_FA_MUSIC, "Unlock Sound"), "UnlockSoundName", "sounds/achievements/unlock.wav",
			FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Select Unlock Sound"));
		draw_sound_setting(FSUI_ICONSTR(ICON_FA_MUSIC, "Leaderboard Submit Sound"), "LBSubmitSoundName",
			"sounds/achievements/lbsubmit.wav", FSUI_ICONSTR(ICON_FA_FOLDER_OPEN, "Select Leaderboard Submit Sound"));

		MenuHeading(FSUI_CSTR("Account"));
		SettingsInterface* secrets_si = Host::Internal::GetSecretsSettingsLayer();
		const TinyString username = bsi->GetTinyStringValue("Achievements", "Username", "");
		const bool has_token = (secrets_si && secrets_si->ContainsValue("Achievements", "Token"));
		if (has_token)
		{
			ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyle().Colors[ImGuiCol_Text]);
			ActiveButton(SmallString::from_format(
							 fmt::runtime(FSUI_ICONSTR(ICON_FA_USER, "Username: {}")), username.empty() ? "Unknown" : username.view()),
				false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
			ActiveButton(SmallString::from_format(fmt::runtime(FSUI_ICONSTR(ICON_FA_CLOCK, "Login token generated on {}")),
							 TimeToPrintableString(static_cast<time_t>(
								 StringUtil::FromChars<u64>(bsi->GetTinyStringValue("Achievements", "LoginTimestamp", "0")).value_or(0)))),
				false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
			ImGui::PopStyleColor();

			if (MenuButton(FSUI_ICONSTR(ICON_FA_KEY, "Logout"), FSUI_CSTR("Logs out of RetroAchievements.")))
			{
				Host::RunOnCPUThread([]() { Achievements::Logout(); });
			}
		}
		else
		{
			ActiveButton(FSUI_ICONSTR(ICON_FA_USER, "Not Logged In"), false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

			if (MenuButton(FSUI_ICONSTR(ICON_FA_KEY, "Login"), FSUI_CSTR("Logs in to RetroAchievements.")))
			{
				s_achievements_login_reason = Achievements::LoginRequestReason::UserInitiated;
				s_achievements_login_show_dismiss = false;
				s_achievements_login_open = true;
			}
		}

		MenuHeading(FSUI_CSTR("Current Game"));
		if (Achievements::HasActiveGame())
		{
			const auto lock = Achievements::GetLock();

			ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyle().Colors[ImGuiCol_Text]);
			ActiveButton(SmallString::from_format(fmt::runtime(FSUI_ICONSTR(ICON_FA_BOOKMARK, "Game: {0} ({1})")), Achievements::GetGameID(),
							 Achievements::GetGameTitle()),
				false, false, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

			const std::string& rich_presence_string = Achievements::GetRichPresenceString();
			if (!rich_presence_string.empty())
			{
				ActiveButton(
					SmallString::from_format(ICON_FA_MAP "{}", rich_presence_string), false, false, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
			}
			else
			{
				ActiveButton(FSUI_ICONSTR(ICON_FA_MAP, "Rich presence inactive or unsupported."), false, false,
					LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
			}

			ImGui::PopStyleColor();
		}
		else
		{
			ActiveButton(FSUI_ICONSTR(ICON_FA_BAN, "Game not loaded or no RetroAchievements available."), false, false,
				LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
		}
	}

	EndMenuButtons();
}

void FullscreenUI::ReportStateLoadError(const std::string& message, std::optional<s32> slot, bool backup)
{
	MTGS::RunOnGSThread([message, slot, backup]() {
		const bool prompt_on_error = Host::GetBaseBoolSettingValue("UI", "PromptOnStateLoadSaveFailure", true);
		if (!prompt_on_error || !ImGuiManager::InitializeFullscreenUI())
		{
			SaveState_ReportLoadErrorOSD(message, slot, backup);
			return;
		}

		std::string title;
		if (slot.has_value())
		{
			if (backup)
				title = fmt::format(FSUI_FSTR("Failed to Load State From Backup Slot {}"), *slot);
			else
				title = fmt::format(FSUI_FSTR("Failed to Load State From Slot {}"), *slot);
		}
		else
		{
			title = FSUI_STR("Failed to Load State");
		}

		ImGuiFullscreen::InfoMessageDialogCallback callback;
		if (VMManager::GetState() == VMState::Running)
		{
			Host::RunOnCPUThread([]() { VMManager::SetPaused(true); });
			callback = []() {
				Host::RunOnCPUThread([]() { VMManager::SetPaused(false); });
			};
		}

		ImGuiFullscreen::OpenInfoMessageDialog(
			fmt::format("{} {}", ICON_FA_TRIANGLE_EXCLAMATION, title),
			std::move(message), std::move(callback));
	});
}

void FullscreenUI::ReportStateSaveError(const std::string& message, std::optional<s32> slot)
{
	MTGS::RunOnGSThread([message, slot]() {
		const bool prompt_on_error = Host::GetBaseBoolSettingValue("UI", "PromptOnStateLoadSaveFailure", true);
		if (!prompt_on_error || !ImGuiManager::InitializeFullscreenUI())
		{
			SaveState_ReportSaveErrorOSD(message, slot);
			return;
		}

		std::string title;
		if (slot.has_value())
			title = fmt::format(FSUI_FSTR("Failed to Save State To Slot {}"), *slot);
		else
			title = FSUI_STR("Failed to Save State");

		ImGuiFullscreen::InfoMessageDialogCallback callback;
		if (VMManager::GetState() == VMState::Running)
		{
			Host::RunOnCPUThread([]() { VMManager::SetPaused(true); });
			callback = []() {
				Host::RunOnCPUThread([]() { VMManager::SetPaused(false); });
			};
		}

		ImGuiFullscreen::OpenInfoMessageDialog(
			fmt::format("{} {}", ICON_FA_TRIANGLE_EXCLAMATION, title),
			std::move(message), std::move(callback));
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Translation String Area
// To avoid having to type T_RANSLATE("FullscreenUI", ...) everywhere, we use the shorter macros in the internal
// header file, then preprocess and generate a bunch of noops here to define the strings. Sadly that means
// the view in Linguist is gonna suck, but you can search the file for the string for more context.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
// TRANSLATION-STRING-AREA-BEGIN
TRANSLATE_NOOP("FullscreenUI", "Error");
TRANSLATE_NOOP("FullscreenUI", "Could not find any CD/DVD-ROM devices. Please ensure you have a drive connected and sufficient permissions to access it.");
TRANSLATE_NOOP("FullscreenUI", "Your memory card is still saving data.\n\nWARNING: Shutting down now can IRREVERSIBLY CORRUPT YOUR MEMORY CARD.\n\nYou are strongly advised to select 'No' and let the save finish.\n\nDo you want to shutdown anyway and IRREVERSIBLY CORRUPT YOUR MEMORY CARD?");
TRANSLATE_NOOP("FullscreenUI", "No save present in this slot.");
TRANSLATE_NOOP("FullscreenUI", "No save states found.");
TRANSLATE_NOOP("FullscreenUI", "Failed to delete save state.");
TRANSLATE_NOOP("FullscreenUI", "empty title");
TRANSLATE_NOOP("FullscreenUI", "no serial");
TRANSLATE_NOOP("FullscreenUI", "Failed to copy text to clipboard.");
TRANSLATE_NOOP("FullscreenUI", "Enable Achievements");
TRANSLATE_NOOP("FullscreenUI", "Achievement tracking is not currently enabled. Your login will have no effect until after tracking is enabled.\n\nDo you want to enable tracking now?");
TRANSLATE_NOOP("FullscreenUI", "Enable Hardcore Mode");
TRANSLATE_NOOP("FullscreenUI", "Hardcore mode is not currently enabled. Enabling hardcore mode allows you to set times, scores, and participate in game-specific leaderboards.\n\nHowever, hardcore mode also prevents the usage of save states, cheats and slowdown functionality.\n\nDo you want to enable hardcore mode?");
TRANSLATE_NOOP("FullscreenUI", "Reset System");
TRANSLATE_NOOP("FullscreenUI", "Hardcore mode will not be enabled until the system is reset. Do you want to reset the system now?");
TRANSLATE_NOOP("FullscreenUI", "This game has no achievements.");
TRANSLATE_NOOP("FullscreenUI", "This game has no leaderboards.");
TRANSLATE_NOOP("FullscreenUI", "No file selected.");
TRANSLATE_NOOP("FullscreenUI", "Sound reset to default.");
TRANSLATE_NOOP("FullscreenUI", "Sound is already using default.");
TRANSLATE_NOOP("FullscreenUI", "Failed to Load State");
TRANSLATE_NOOP("FullscreenUI", "Failed to Save State");
TRANSLATE_NOOP("FullscreenUI", "Game List");
TRANSLATE_NOOP("FullscreenUI", "Launch a game from images scanned from your game directories.");
TRANSLATE_NOOP("FullscreenUI", "Start Game");
TRANSLATE_NOOP("FullscreenUI", "Launch a game from a file, disc, or starts the console without any disc inserted.");
TRANSLATE_NOOP("FullscreenUI", "Settings");
TRANSLATE_NOOP("FullscreenUI", "Changes settings for the application.");
TRANSLATE_NOOP("FullscreenUI", "Exit");
TRANSLATE_NOOP("FullscreenUI", "Return to desktop mode, or exit the application.");
TRANSLATE_NOOP("FullscreenUI", "Start File");
TRANSLATE_NOOP("FullscreenUI", "Launch a game by selecting a file/disc image.");
TRANSLATE_NOOP("FullscreenUI", "Start Disc");
TRANSLATE_NOOP("FullscreenUI", "Start a game from a disc in your PC's DVD drive.");
TRANSLATE_NOOP("FullscreenUI", "Start BIOS");
TRANSLATE_NOOP("FullscreenUI", "Start the console without any disc inserted.");
TRANSLATE_NOOP("FullscreenUI", "Back");
TRANSLATE_NOOP("FullscreenUI", "Return to the previous menu.");
TRANSLATE_NOOP("FullscreenUI", "Exit PCSX2");
TRANSLATE_NOOP("FullscreenUI", "Completely exits the application, returning you to your desktop.");
TRANSLATE_NOOP("FullscreenUI", "Desktop Mode");
TRANSLATE_NOOP("FullscreenUI", "Exits Big Picture mode, returning to the desktop interface.");
TRANSLATE_NOOP("FullscreenUI", "Load State");
TRANSLATE_NOOP("FullscreenUI", "Save State");
TRANSLATE_NOOP("FullscreenUI", "Load Resume State");
TRANSLATE_NOOP("FullscreenUI", "A resume save state created at %s was found.\n\nDo you want to load this save and continue?");
TRANSLATE_NOOP("FullscreenUI", "Region: ");
TRANSLATE_NOOP("FullscreenUI", "Compatibility: ");
TRANSLATE_NOOP("FullscreenUI", "No Game Selected");
TRANSLATE_NOOP("FullscreenUI", "Game List Settings");
TRANSLATE_NOOP("FullscreenUI", "Search Directories");
TRANSLATE_NOOP("FullscreenUI", "Adds a new directory to the game search list.");
TRANSLATE_NOOP("FullscreenUI", "Scanning Subdirectories");
TRANSLATE_NOOP("FullscreenUI", "Not Scanning Subdirectories");
TRANSLATE_NOOP("FullscreenUI", "List Settings");
TRANSLATE_NOOP("FullscreenUI", "Sets which view the game list will open to.");
TRANSLATE_NOOP("FullscreenUI", "Determines which field the game list will be sorted by.");
TRANSLATE_NOOP("FullscreenUI", "Reverses the game list sort order from the default (usually ascending to descending).");
TRANSLATE_NOOP("FullscreenUI", "Shows Titles for Games when in Game Grid View Mode");
TRANSLATE_NOOP("FullscreenUI", "Cover Settings");
TRANSLATE_NOOP("FullscreenUI", "Downloads covers from a user-specified URL template.");
TRANSLATE_NOOP("FullscreenUI", "Operations");
TRANSLATE_NOOP("FullscreenUI", "Identifies any new files added to the game directories.");
TRANSLATE_NOOP("FullscreenUI", "Forces a full rescan of all games previously identified.");
TRANSLATE_NOOP("FullscreenUI", "About PCSX2");
TRANSLATE_NOOP("FullscreenUI", "PCSX2 is a free and open-source PlayStation 2 (PS2) emulator. Its purpose is to emulate the PS2's hardware, using a combination of MIPS CPU Interpreters, Recompilers and a Virtual Machine which manages hardware states and PS2 system memory. This allows you to play PS2 games on your PC, with many additional features and benefits.");
TRANSLATE_NOOP("FullscreenUI", "PlayStation 2 and PS2 are registered trademarks of Sony Interactive Entertainment. This application is not affiliated in any way with Sony Interactive Entertainment.");
TRANSLATE_NOOP("FullscreenUI", "RetroAchievements");
TRANSLATE_NOOP("FullscreenUI", "Please enter your user name and password for retroachievements.org below.\n\nYour password will not be saved in PCSX2, an access token will be generated and used instead.");
TRANSLATE_NOOP("FullscreenUI", "Username");
TRANSLATE_NOOP("FullscreenUI", "Password");
TRANSLATE_NOOP("FullscreenUI", "Logging in...");
TRANSLATE_NOOP("FullscreenUI", "Dismiss");
TRANSLATE_NOOP("FullscreenUI", "Login");
TRANSLATE_NOOP("FullscreenUI", "Cancel");
TRANSLATE_NOOP("FullscreenUI", "When enabled and logged in, PCSX2 will scan for achievements on startup.");
TRANSLATE_NOOP("FullscreenUI", "\"Challenge\" mode for achievements, including leaderboard tracking. Disables save state, cheats, and slowdown functions.");
TRANSLATE_NOOP("FullscreenUI", "Displays popup messages on events such as achievement unlocks and leaderboard submissions.");
TRANSLATE_NOOP("FullscreenUI", "Displays popup messages when starting, submitting, or failing a leaderboard challenge.");
TRANSLATE_NOOP("FullscreenUI", "Plays sound effects for events such as achievement unlocks and leaderboard submissions.");
TRANSLATE_NOOP("FullscreenUI", "Shows icons in the screen when a challenge/primed achievement is active.");
TRANSLATE_NOOP("FullscreenUI", "Shows icons in the screen when leaderboard tracking is active.");
TRANSLATE_NOOP("FullscreenUI", "Determines where achievement/leaderboard overlays are positioned on the screen.");
TRANSLATE_NOOP("FullscreenUI", "Determines where achievement/leaderboard notification popups are positioned on the screen.");
TRANSLATE_NOOP("FullscreenUI", "When enabled, each session will behave as if no achievements have been unlocked.");
TRANSLATE_NOOP("FullscreenUI", "When enabled, PCSX2 will assume all achievements are locked and not send any unlock notifications to the server.");
TRANSLATE_NOOP("FullscreenUI", "When enabled, PCSX2 will list achievements from unofficial sets. These achievements are not tracked by RetroAchievements.");
TRANSLATE_NOOP("FullscreenUI", "Sound Effects");
TRANSLATE_NOOP("FullscreenUI", "Account");
TRANSLATE_NOOP("FullscreenUI", "Logs out of RetroAchievements.");
TRANSLATE_NOOP("FullscreenUI", "Logs in to RetroAchievements.");
TRANSLATE_NOOP("FullscreenUI", "Current Game");
TRANSLATE_NOOP("FullscreenUI", "An error occurred while deleting empty game settings:\n{}");
TRANSLATE_NOOP("FullscreenUI", "An error occurred while saving game settings:\n{}");
TRANSLATE_NOOP("FullscreenUI", "{} is not a valid disc image.");
TRANSLATE_NOOP("FullscreenUI", "{:%H:%M}");
TRANSLATE_NOOP("FullscreenUI", "This Session: {}");
TRANSLATE_NOOP("FullscreenUI", "All Time: {}");
TRANSLATE_NOOP("FullscreenUI", "Save Slot {0}");
TRANSLATE_NOOP("FullscreenUI", "{0} Slot {1}");
TRANSLATE_NOOP("FullscreenUI", "Saved {}");
TRANSLATE_NOOP("FullscreenUI", "{} does not exist.");
TRANSLATE_NOOP("FullscreenUI", "{} deleted.");
TRANSLATE_NOOP("FullscreenUI", "Failed to delete {}.");
TRANSLATE_NOOP("FullscreenUI", "File: {}");
TRANSLATE_NOOP("FullscreenUI", "CRC: {:08X}");
TRANSLATE_NOOP("FullscreenUI", "Time Played: {}");
TRANSLATE_NOOP("FullscreenUI", "Last Played: {}");
TRANSLATE_NOOP("FullscreenUI", "Size: {:.2f} MB");
TRANSLATE_NOOP("FullscreenUI", "Are you sure you want to reset the play time for '{}' ({})?\n\nYour current play time is {}.\n\nThis action cannot be undone.");
TRANSLATE_NOOP("FullscreenUI", "Version: {}");
TRANSLATE_NOOP("FullscreenUI", "Successfully logged in as {}.");
TRANSLATE_NOOP("FullscreenUI", "Login failed.\nError: {}\n\nPlease check your username and password, and try again.");
TRANSLATE_NOOP("FullscreenUI", "Failed to preview sound:\n{}");
TRANSLATE_NOOP("FullscreenUI", "Failed to Load State From Backup Slot {}");
TRANSLATE_NOOP("FullscreenUI", "Failed to Load State From Slot {}");
TRANSLATE_NOOP("FullscreenUI", "Failed to Save State To Slot {}");
TRANSLATE_NOOP("FullscreenUI", "Game Grid");
TRANSLATE_NOOP("FullscreenUI", "Type");
TRANSLATE_NOOP("FullscreenUI", "Serial");
TRANSLATE_NOOP("FullscreenUI", "Title");
TRANSLATE_NOOP("FullscreenUI", "File Title");
TRANSLATE_NOOP("FullscreenUI", "CRC");
TRANSLATE_NOOP("FullscreenUI", "Time Played");
TRANSLATE_NOOP("FullscreenUI", "Last Played");
TRANSLATE_NOOP("FullscreenUI", "Size");
TRANSLATE_NOOP("FullscreenUI", "Change Selection");
TRANSLATE_NOOP("FullscreenUI", "Select");
TRANSLATE_NOOP("FullscreenUI", "Parent Directory");
TRANSLATE_NOOP("FullscreenUI", "Enter Value");
TRANSLATE_NOOP("FullscreenUI", "About");
TRANSLATE_NOOP("FullscreenUI", "Navigate");
TRANSLATE_NOOP("FullscreenUI", "Toggle Fullscreen");
TRANSLATE_NOOP("FullscreenUI", "Load Global State");
TRANSLATE_NOOP("FullscreenUI", "Return To Game");
TRANSLATE_NOOP("FullscreenUI", "Select State");
TRANSLATE_NOOP("FullscreenUI", "Options");
TRANSLATE_NOOP("FullscreenUI", "Load/Save State");
TRANSLATE_NOOP("FullscreenUI", "Select Game");
TRANSLATE_NOOP("FullscreenUI", "Change View");
TRANSLATE_NOOP("FullscreenUI", "Launch Options");
TRANSLATE_NOOP("FullscreenUI", "Startup Error");
TRANSLATE_NOOP("FullscreenUI", "Select Disc Image");
TRANSLATE_NOOP("FullscreenUI", "Select Disc Drive");
TRANSLATE_NOOP("FullscreenUI", "WARNING: Memory Card Busy");
TRANSLATE_NOOP("FullscreenUI", "Resume Game");
TRANSLATE_NOOP("FullscreenUI", "Close Game");
TRANSLATE_NOOP("FullscreenUI", "Back To Pause Menu");
TRANSLATE_NOOP("FullscreenUI", "Exit Without Saving");
TRANSLATE_NOOP("FullscreenUI", "Leaderboards");
TRANSLATE_NOOP("FullscreenUI", "Toggle Frame Limit");
TRANSLATE_NOOP("FullscreenUI", "Game Properties");
TRANSLATE_NOOP("FullscreenUI", "Achievements");
TRANSLATE_NOOP("FullscreenUI", "Save Screenshot");
TRANSLATE_NOOP("FullscreenUI", "Switch To Software Renderer");
TRANSLATE_NOOP("FullscreenUI", "Switch To Hardware Renderer");
TRANSLATE_NOOP("FullscreenUI", "Change Disc");
TRANSLATE_NOOP("FullscreenUI", "Exit And Save State");
TRANSLATE_NOOP("FullscreenUI", "Delete Save");
TRANSLATE_NOOP("FullscreenUI", "Close Menu");
TRANSLATE_NOOP("FullscreenUI", "Default Boot");
TRANSLATE_NOOP("FullscreenUI", "Delete State");
TRANSLATE_NOOP("FullscreenUI", "Fast Boot");
TRANSLATE_NOOP("FullscreenUI", "Full Boot");
TRANSLATE_NOOP("FullscreenUI", "Reset Play Time");
TRANSLATE_NOOP("FullscreenUI", "Confirm Reset");
TRANSLATE_NOOP("FullscreenUI", "Add Search Directory");
TRANSLATE_NOOP("FullscreenUI", "Open in File Browser");
TRANSLATE_NOOP("FullscreenUI", "Disable Subdirectory Scanning");
TRANSLATE_NOOP("FullscreenUI", "Enable Subdirectory Scanning");
TRANSLATE_NOOP("FullscreenUI", "Remove From List");
TRANSLATE_NOOP("FullscreenUI", "Default View");
TRANSLATE_NOOP("FullscreenUI", "Sort By");
TRANSLATE_NOOP("FullscreenUI", "Sort Reversed");
TRANSLATE_NOOP("FullscreenUI", "Show Titles");
TRANSLATE_NOOP("FullscreenUI", "Covers Directory");
TRANSLATE_NOOP("FullscreenUI", "Download Covers");
TRANSLATE_NOOP("FullscreenUI", "Scan For New Games");
TRANSLATE_NOOP("FullscreenUI", "Rescan All Games");
TRANSLATE_NOOP("FullscreenUI", "Website");
TRANSLATE_NOOP("FullscreenUI", "Support Forums");
TRANSLATE_NOOP("FullscreenUI", "GitHub Repository");
TRANSLATE_NOOP("FullscreenUI", "License");
TRANSLATE_NOOP("FullscreenUI", "Close");
TRANSLATE_NOOP("FullscreenUI", "RAIntegration is being used instead of the built-in achievements implementation.");
TRANSLATE_NOOP("FullscreenUI", "Hardcore Mode");
TRANSLATE_NOOP("FullscreenUI", "Achievement Notifications");
TRANSLATE_NOOP("FullscreenUI", "Leaderboard Notifications");
TRANSLATE_NOOP("FullscreenUI", "Enable In-Game Overlays");
TRANSLATE_NOOP("FullscreenUI", "Enable In-Game Leaderboard Overlays");
TRANSLATE_NOOP("FullscreenUI", "Overlay Position");
TRANSLATE_NOOP("FullscreenUI", "Notification Position");
TRANSLATE_NOOP("FullscreenUI", "Encore Mode");
TRANSLATE_NOOP("FullscreenUI", "Spectator Mode");
TRANSLATE_NOOP("FullscreenUI", "Test Unofficial Achievements");
TRANSLATE_NOOP("FullscreenUI", "Select File");
TRANSLATE_NOOP("FullscreenUI", "Preview");
TRANSLATE_NOOP("FullscreenUI", "Reset to Default");
TRANSLATE_NOOP("FullscreenUI", "Notification Sound");
TRANSLATE_NOOP("FullscreenUI", "Select Notification Sound");
TRANSLATE_NOOP("FullscreenUI", "Unlock Sound");
TRANSLATE_NOOP("FullscreenUI", "Select Unlock Sound");
TRANSLATE_NOOP("FullscreenUI", "Leaderboard Submit Sound");
TRANSLATE_NOOP("FullscreenUI", "Select Leaderboard Submit Sound");
TRANSLATE_NOOP("FullscreenUI", "Username: {}");
TRANSLATE_NOOP("FullscreenUI", "Login token generated on {}");
TRANSLATE_NOOP("FullscreenUI", "Logout");
TRANSLATE_NOOP("FullscreenUI", "Not Logged In");
TRANSLATE_NOOP("FullscreenUI", "Game: {0} ({1})");
TRANSLATE_NOOP("FullscreenUI", "Rich presence inactive or unsupported.");
TRANSLATE_NOOP("FullscreenUI", "Game not loaded or no RetroAchievements available.");
// TRANSLATION-STRING-AREA-END
#endif
