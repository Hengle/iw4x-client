#include "STDInclude.hpp"

namespace Components
{
	std::vector<std::string> Menus::CustomMenus;
	std::map<std::string, Game::menuDef_t*> Menus::MenuList;
	std::map<std::string, Game::MenuList*> Menus::MenuListList;

	int Menus::ReserveSourceHandle()
	{
		// Check if a free slot is available
		int i = 1;
		for (; i < MAX_SOURCEFILES; i++)
		{
			if (!Game::sourceFiles[i])
				break;
		}

		if (i >= MAX_SOURCEFILES)
			return 0;

		// Reserve it, if yes
		Game::sourceFiles[i] = (Game::source_t*)1;

		return i;
	}

	Game::script_t* Menus::LoadMenuScript(std::string name, std::string buffer)
	{
		Game::script_t* script = Game::Script_Alloc(sizeof(Game::script_t) + 1 + buffer.length());

		strcpy_s(script->filename, sizeof(script->filename), name.data());
		script->buffer = (char*)(script + 1);

		*((char*)(script + 1) + buffer.length()) = '\0';

		script->script_p = script->buffer;
		script->lastscript_p = script->buffer;
		script->length = buffer.length();
		script->end_p = &script->buffer[buffer.length()];
		script->line = 1;
		script->lastline = 1;
		script->tokenavailable = 0;

		Game::Script_SetupTokens(script, (void*)0x797F80);
		script->punctuations = (Game::punctuation_t*)0x797F80;

		strcpy(script->buffer, buffer.data());

		script->length = Game::Script_CleanString(script->buffer);

		return script;
	}

	int Menus::LoadMenuSource(std::string name, std::string buffer)
	{
		int handle = Menus::ReserveSourceHandle();
		if (!Menus::IsValidSourceHandle(handle)) return 0; // No free source slot!

		Game::source_t *source = nullptr;
		Game::script_t *script = Menus::LoadMenuScript(name, buffer);

		if (!script)
		{
			Game::sourceFiles[handle] = nullptr; // Free reserved slot
			return 0;
		}

		script->next = NULL;

		source = Utils::Memory::AllocateArray<Game::source_t>(1);
		if (!source)
		{
			Game::FreeMemory(script);
			return 0;
		}

		strncpy(source->filename, "string", 64);
		source->scriptstack = script;
		source->tokens = NULL;
		source->defines = NULL;
		source->indentstack = NULL;
		source->skip = 0;
		source->definehash = (Game::define_t**)Utils::Memory::Allocate(4096);

		Game::sourceFiles[handle] = source;

		return handle;
	}

	bool Menus::IsValidSourceHandle(int handle)
	{
		return (handle > 0 && handle < MAX_SOURCEFILES && Game::sourceFiles[handle]);
	}

	int Menus::KeywordHash(char* key)
	{
		// patch this function on-the-fly, as it's some ugly C.
		Utils::Hook::Set<DWORD>(0x63FE9E, 3523);
		Utils::Hook::Set<DWORD>(0x63FECB, 0x7F);

		int var = 0x63FE90;
		__asm
		{
			mov eax, key
			call var
			mov var, eax
		}

		Utils::Hook::Set<DWORD>(0x63FE9E, 531);
		Utils::Hook::Set<DWORD>(0x63FECB, 0x1FF);

		return var;
	}

	Game::menuDef_t* Menus::ParseMenu(int handle)
	{
		Game::menuDef_t* menu = Utils::Memory::AllocateArray<Game::menuDef_t>(1);
		if (!menu) return nullptr;

		menu->items = Utils::Memory::AllocateArray<Game::itemDef_t*>(512);
		if (!menu->items) 
		{
			Utils::Memory::Free(menu);
			return nullptr;
		}

		Game::pc_token_t token;
		Game::keywordHash_t *key;

		if (!Game::PC_ReadTokenHandle(handle, &token) || token.string[0] != '{')
		{
			Utils::Memory::Free(menu->items);
			Utils::Memory::Free(menu);
			return nullptr;
		}

		while (true) 
		{
			ZeroMemory(&token, sizeof(token));

			if (!Game::PC_ReadTokenHandle(handle, &token))
			{
				Game::PC_SourceError(handle, "end of file inside menu\n");
				break; // Fail
			}

			if (*token.string == '}') 
			{
				break; // Success
			}

			int idx = Menus::KeywordHash(token.string);

			key = Game::menuParseKeywordHash[idx];

			if (!key) 
			{
				Game::PC_SourceError(handle, "unknown menu keyword %s", token.string);
				continue;
			}

			if (!key->func(menu, handle)) 
			{
				Game::PC_SourceError(handle, "couldn't parse menu keyword %s", token.string);
				break; // Fail
			}
		}

		Menus::RemoveMenu(menu->window.name);
		Menus::MenuList[menu->window.name] = menu;

		return menu;
	}

	std::vector<Game::menuDef_t*> Menus::LoadMenu(std::string menu)
	{
		std::vector<Game::menuDef_t*> menus;
		FileSystem::File menuFile(menu);

		if (menuFile.Exists())
		{
			Game::pc_token_t token;
			int handle = Menus::LoadMenuSource(menu, menuFile.GetBuffer());

			if (Menus::IsValidSourceHandle(handle))
			{
				while (true)
				{
					ZeroMemory(&token, sizeof(token));

					if (!Game::PC_ReadTokenHandle(handle, &token) || token.string[0] == '}')
					{
						break;
					}

					if (!_stricmp(token.string, "loadmenu"))
					{
						Game::PC_ReadTokenHandle(handle, &token);

						Utils::Merge(menus, Menus::LoadMenu(Utils::VA("ui_mp\\%s.menu", token.string)));
					}

					if (!_stricmp(token.string, "menudef"))
					{
						Game::menuDef_t* menudef = Menus::ParseMenu(handle);
						if (menudef) menus.push_back(menudef);
					}
				}

				Menus::FreeMenuSource(handle);
			}
		}

		return menus;
	}

	std::vector<Game::menuDef_t*> Menus::LoadMenu(Game::menuDef_t* menudef)
	{
		std::vector<Game::menuDef_t*> menus = Menus::LoadMenu(Utils::VA("ui_mp\\%s.menu", menudef->window.name));

		if (!menus.size())
		{
			menus.push_back(menudef);
		}

		return menus;
	}

	Game::MenuList* Menus::LoadScriptMenu(const char* menu)
	{
		std::vector<Game::menuDef_t*> menus = Menus::LoadMenu(menu);
		if (!menus.size()) return nullptr;

		// Allocate new menu list
		Game::MenuList* newList = Utils::Memory::AllocateArray<Game::MenuList>(1);
		if (!newList) return nullptr;

		newList->menus = Utils::Memory::AllocateArray<Game::menuDef_t*>(menus.size());
		if (!newList->menus)
		{
			Utils::Memory::Free(newList);
			return nullptr;
		}

		newList->name = Utils::Memory::DuplicateString(menu);
		newList->menuCount = menus.size();

		// Copy new menus
		memcpy(newList->menus, menus.data(), menus.size() * sizeof(Game::menuDef_t *));

		Menus::RemoveMenuList(newList->name);
		Menus::MenuListList[newList->name] = newList;

		return newList;
	}

	Game::MenuList* Menus::LoadMenuList(Game::MenuList* menuList)
	{
		std::vector<Game::menuDef_t*> menus;

		for (int i = 0; i < menuList->menuCount; i++)
		{
			if (!menuList->menus[i])
			{
				continue;
			}

			Utils::Merge(menus, Menus::LoadMenu(menuList->menus[i]));
		}

		// Load custom menus
		if (std::string(menuList->name) == "ui_mp/code.txt") // Should be menus, but code is loaded ingame
		{
			for (auto menu : Menus::CustomMenus)
			{
				Utils::Merge(menus, Menus::LoadMenu(menu));
			}
		}

		// Allocate new menu list
		Game::MenuList* newList = Utils::Memory::AllocateArray<Game::MenuList>(1);
		if (!newList) return menuList;

		newList->menus = Utils::Memory::AllocateArray<Game::menuDef_t*>(menus.size());
		if (!newList->menus)
		{
			Utils::Memory::Free(newList);
			return menuList;
		}

		newList->name = Utils::Memory::DuplicateString(menuList->name);
		newList->menuCount = menus.size();

		// Copy new menus
		memcpy(newList->menus, menus.data(), menus.size() * sizeof(Game::menuDef_t *));

		Menus::RemoveMenuList(newList->name);
		Menus::MenuListList[newList->name] = newList;

		return newList;
	}

	void Menus::FreeMenuSource(int handle)
	{
		if (!Menus::IsValidSourceHandle(handle)) return;

		Game::script_t *script;
 		Game::token_t *token;
 		Game::define_t *define;
		Game::indent_t *indent;
		Game::source_t *source = Game::sourceFiles[handle];

		while (source->scriptstack)
		{
			script = source->scriptstack;
			source->scriptstack = source->scriptstack->next;
			Game::FreeMemory(script);
		}

		while (source->tokens)
		{
			token = source->tokens;
			source->tokens = source->tokens->next;
			Game::FreeMemory(token);
		}

		while (source->defines)
		{
			define = source->defines;
			source->defines = source->defines->next;
			Game::FreeMemory(define);
		}

		while (source->indentstack)
		{
			indent = source->indentstack;
			source->indentstack = source->indentstack->next;
			Utils::Memory::Free(indent);
		}

		if (source->definehash) Utils::Memory::Free(source->definehash);

		Utils::Memory::Free(source);

		Game::sourceFiles[handle] = nullptr;
	}

	void Menus::FreeMenu(Game::menuDef_t* menudef)
	{
		// Do i need to free expressions and strings?
		// Or does the game take care of it?
		// Seems like it does...

		if (menudef->items)
		{
			// Seems like this is obsolete as well,
			// as the game handles the memory

			//for (int i = 0; i < menudef->itemCount; i++)
			//{
			//	Game::Menu_FreeItemMemory(menudef->items[i]);
			//}

			Utils::Memory::Free(menudef->items);
		}

		Utils::Memory::Free(menudef);
	}

	void Menus::FreeMenuList(Game::MenuList* menuList)
	{
		if (!menuList) return;

		// Keep our compiler happy
		Game::MenuList list = { menuList->name, menuList->menuCount, menuList->menus };

		if (list.name)
		{
			Utils::Memory::Free(list.name);
		}

		if (list.menus)
		{
			Utils::Memory::Free(list.menus);
		}

		Utils::Memory::Free(menuList);
	}

	void Menus::RemoveMenu(std::string menu)
	{
		auto i = Menus::MenuList.find(menu);
		if(i != Menus::MenuList.end())
		{
			if (i->second) Menus::FreeMenu(i->second);
			Menus::MenuList.erase(i);
		}
	}

	void Menus::RemoveMenu(Game::menuDef_t* menudef)
	{
		for (auto i = Menus::MenuList.begin(); i != Menus::MenuList.end(); i++)
		{
			if (i->second == menudef)
			{
				Menus::FreeMenu(menudef);
				Menus::MenuList.erase(i);
				break;
			}
		}
	}

	void Menus::RemoveMenuList(std::string menuList)
	{
		auto i = Menus::MenuListList.find(menuList);
		if (i != Menus::MenuListList.end())
		{
			if (i->second)
			{
				for (auto j = 0; j < i->second->menuCount; j++)
				{
					Menus::RemoveMenu(i->second->menus[j]);
				}

				Menus::FreeMenuList(i->second);
			}

			Menus::MenuListList.erase(i);
		}
	}

	void Menus::RemoveMenuList(Game::MenuList* menuList)
	{
		if (!menuList || !menuList->name) return;
		Menus::RemoveMenuList(menuList->name);
	}

	void Menus::FreeEverything()
	{
		for (auto i = Menus::MenuListList.begin(); i != Menus::MenuListList.end(); i++)
		{
			Menus::FreeMenuList(i->second);
		}

		Menus::MenuListList.clear();

		for (auto i = Menus::MenuList.begin(); i != Menus::MenuList.end(); i++)
		{
			Menus::FreeMenu(i->second);
		}

		Menus::MenuList.clear();
	}

	Game::XAssetHeader Menus::MenuLoad(Game::XAssetType type, const char* filename)
	{
		return { Menus::FindMenuByName(Game::uiContext, filename) };
	}

	Game::XAssetHeader Menus::MenuFileLoad(Game::XAssetType type, const char* filename)
	{
 		Game::XAssetHeader header = { 0 };

		// Check if we already loaded it and free it, seems like the game deallocated it
		Menus::RemoveMenuList(Menus::MenuListList[filename]);

		Game::MenuList* menuList = Game::DB_FindXAssetHeader(type, filename).menuList;
		header.menuList = menuList;
		
		if (menuList)
		{
			// Parse scriptmenus!
			if (!strcmp(menuList->menus[0]->window.name, "default_menu") || Utils::EndsWith(filename, ".menu"))
			{
				if (FileSystem::File(filename).Exists())
				{
					header.menuList = Menus::LoadScriptMenu(filename);

					// Reset, if we didn't find scriptmenus
					if (!header.menuList)
					{
						header.menuList = menuList;
					}
				}
			}
			else
			{
				header.menuList = Menus::LoadMenuList(menuList);
			}
		}

		return header;
	}

	void Menus::AddMenuListHook(Game::UiContext *dc, Game::MenuList *menuList, int close)
	{
		Menus::ReloadStack(dc);

		Game::MenuList* menus = Game::UI_LoadMenus("ui_mp/menus.txt", 3);

		Game::UI_AddMenuList(dc, menus, close);
		Game::UI_AddMenuList(dc, menuList, close);
	}

	void Menus::RemoveFromStack(Game::UiContext *dc, Game::menuDef_t *menu)
	{
		// Search menu in stack
		int i = 0;
		for (; i < dc->menuCount; i++)
		{
			if (dc->Menus[i] == menu)
			{
				break;
			}
		}

		// Remove from stack
		if (i < dc->menuCount)
		{
			for (; i < dc->menuCount - 1; i++)
			{
				dc->Menus[i] = dc->Menus[i + 1];
			}

			// Clear last menu
			dc->Menus[--dc->menuCount] = 0;
		}
	}

	void __declspec(naked) Menus::OpenMenuStub()
	{
		__asm
		{
			// Menu
			mov eax, [esp + 8h]
			push eax

			// Context
			mov eax, [esp + 8h]
			push eax

			call Menus::IsMenuAllowed
			add esp, 8h

			test al, al
			jnz continue

			retn

		continue:
			push ecx
			mov [esp], 0
			mov eax, 4B72F8h
			jmp eax
		}
	}

	void __declspec(naked) Menus::CloseMenuStub()
	{
		__asm
		{
			// Menu
			mov eax, [esp + 8h]
			push eax

			// Context
			mov eax, [esp + 8h]
			push eax

			call Menus::IsMenuAllowed
			add esp, 8h

			test al, al
			jnz continue

			retn

		continue:
			sub esp, 17Ch
			mov eax, 430D56h
			jmp eax
		}
	}

	bool Menus::ReloadMenu(Game::UiContext *dc, Game::menuDef_t *menu)
	{
		// Find the name of our destroyed menu
		std::string name;
		auto i = Menus::MenuList.begin();
		for (; i != Menus::MenuList.end(); i++)
		{
			if (i->second == menu)
			{
				name = i->first;
				break;
			}
		}

		// We have found it, so we can remove and reload it
		if (i != Menus::MenuList.end())
		{
			Menus::FreeMenu(menu);
			Menus::MenuList.erase(i);

			// Load original menu, in case we can't reload a custom one
			Game::menuDef_t* newMenu = AssetHandler::FindOriginalAsset(Game::XAssetType::ASSET_TYPE_MENU, name.data()).menu;

			// Try reloading our custom menu
			auto newMenus = Menus::LoadMenu(name);
			if (newMenus.size()) newMenu = newMenus[0];

			// Now replace the destroyed menu with the new one in the stack
			for (int i = 0; i < dc->menuCount; i++)
			{
				if (dc->Menus[i] == menu)
				{
					dc->Menus[i] = newMenu;
				}
			}

			return true;
		}
		else
		{
			Menus::RemoveFromStack(dc, menu);
		}

		return false;
	}

	void Menus::ReloadStack(Game::UiContext *dc)
	{
		for (int i = 0; i < dc->menuCount; i++)
		{
			Game::menuDef_t* menu = dc->Menus[i];
			if (menu && menu->window.name && IsBadReadPtr(menu->window.name, 1)) // Sanity checks
			{
				Menus::ReloadMenu(dc, menu);
			}
		}
	}

	Game::menuDef_t* Menus::FindMenuByName(Game::UiContext* dc, const char* name)
	{
		for (int i = 0; i < dc->menuCount; i++)
		{
			Game::menuDef_t* menu = dc->Menus[i];
			if (menu && menu->window.name && !IsBadReadPtr(menu->window.name, 1)) // Sanity checks
			{
				if (!_strnicmp(name, menu->window.name, 0x7FFFFFFF))
				{
					return menu;
				}
			}
			else
			{
				Menus::ReloadMenu(dc, menu);
			}
		}

		return nullptr;
	}

	bool Menus::IsMenuAllowed(Game::UiContext *dc, Game::menuDef_t *menu)
	{
		if (menu && menu->window.name && !IsBadReadPtr(menu->window.name, 1))
		{
			if (std::string(menu->window.name) == "connect") // Check if we're supposed to draw the loadscreen
			{
				Game::menuDef_t* originalConnect = AssetHandler::FindOriginalAsset(Game::XAssetType::ASSET_TYPE_MENU, "connect").menu;

				if (originalConnect == menu) // Check if we draw the original loadscreen
				{
					if (Menus::MenuList.find("connect") != Menus::MenuList.end()) // Check if we have a custom loadscreen, to prevent drawing the original one ontop
					{
						Menus::RemoveFromStack(dc, menu);
						return false;
					}
				}
			}

			return true;
		}

		Menus::ReloadMenu(dc, menu);
		return false;
	}

	bool Menus::IsMenuVisible(Game::UiContext *dc, Game::menuDef_t *menu)
	{
		return (Menus::IsMenuAllowed(dc, menu) && Game::Menu_IsVisible(dc, menu));
	}

	void Menus::Add(std::string menu)
	{
		Menus::CustomMenus.push_back(menu);
	}

	Menus::Menus()
	{
		if (Dedicated::IsDedicated()) return;

		// Ensure everything is zero'ed
		Menus::FreeEverything();

		// Intercept asset finding
		AssetHandler::OnFind(Game::XAssetType::ASSET_TYPE_MENU, Menus::MenuLoad);
		AssetHandler::OnFind(Game::XAssetType::ASSET_TYPE_MENUFILE, Menus::MenuFileLoad);

		// Don't open connect menu
		Utils::Hook::Nop(0x428E48, 5);

		// Intercept menu painting
		Utils::Hook(0x4FFBDF, Menus::IsMenuVisible, HOOK_CALL).Install()->Quick();

		// Intercept menu closing
		Utils::Hook(0x430D50, Menus::CloseMenuStub, HOOK_JUMP).Install()->Quick();

		// Intercept menu opening
		Utils::Hook(0x4B72F0, Menus::OpenMenuStub, HOOK_JUMP).Install()->Quick();

		// Custom Menus_FindByName
		Utils::Hook(0x487240, Menus::FindMenuByName, HOOK_JUMP).Install()->Quick();

		// Load menus ingame
		//Utils::Hook(0x41C178, Menus::AddMenuListHook, HOOK_CALL).Install()->Quick();

		// disable the 2 new tokens in ItemParse_rect
		Utils::Hook::Set<BYTE>(0x640693, 0xEB);

		// don't load ASSET_TYPE_MENU assets for every menu (might cause patch menus to fail)
		Utils::Hook::Nop(0x453406, 5);

		//make Com_Error and similar go back to main_text instead of menu_xboxlive.
		Utils::Hook::SetString(0x6FC790, "main_text");

		Command::Add("openmenu", [] (Command::Params params)
		{
			if (params.Length() != 2)
			{
				Logger::Print("USAGE: openmenu <menu name>\n");
				return;
			}

			Game::Menus_OpenByName(Game::uiContext, params[1]);
		});

		Command::Add("reloadmenus", [] (Command::Params params)
		{
			// Close all menus
			Game::Menus_CloseAll(Game::uiContext);

			// Free custom menus
			Menus::FreeEverything();
			
			// Only disconnect if in-game, context is updated automatically!
			if (Game::CL_IsCgameInitialized())
			{
				Game::Cbuf_AddText(0, "disconnect\n");
			}
			else
			{
				// Reinitialize ui context
				((void(*)())0x401700)();

				// Reopen main menu
				Game::Menus_OpenByName(Game::uiContext, "main_text");
			}
		});

		// Define custom menus here
		Menus::Add("ui_mp/theater_menu.menu");
		Menus::Add("ui_mp/pc_options_multi.menu");
		Menus::Add("ui_mp/pc_options_game.menu");
	}

	Menus::~Menus()
	{
		Menus::CustomMenus.clear();
		Menus::FreeEverything();
	}
}
