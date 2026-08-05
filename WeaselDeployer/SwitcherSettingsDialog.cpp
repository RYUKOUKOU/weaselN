#include "stdafx.h"
#include "SwitcherSettingsDialog.h"
#include "Configurator.h"
#include <algorithm>
#include <set>
#include <rime_levers_api.h>
#include <WeaselUtility.h>
#include "WeaselDeployer.h"

namespace {
constexpr wchar_t kWeaselRegKey[] = L"Software\\Rime\\Weasel";
constexpr wchar_t kKeyboardLayoutValue[] = L"KeyboardLayout";
}  // namespace

SwitcherSettingsDialog::SwitcherSettingsDialog(RimeSwitcherSettings* settings)
    : settings_(settings), loaded_(false), modified_(false) {
  api_ = (RimeLeversApi*)rime_get_api()->find_module("levers")->get_api();
}

SwitcherSettingsDialog::~SwitcherSettingsDialog() {}

void SwitcherSettingsDialog::Populate() {
  if (!settings_)
    return;
  RimeSchemaList available = {0};
  api_->get_available_schema_list(settings_, &available);
  RimeSchemaList selected = {0};
  api_->get_selected_schema_list(settings_, &selected);
  schema_list_.DeleteAllItems();
  size_t k = 0;
  std::set<RimeSchemaInfo*> recruited;
  for (size_t i = 0; i < selected.size; ++i) {
    const char* schema_id = selected.list[i].schema_id;
    for (size_t j = 0; j < available.size; ++j) {
      RimeSchemaListItem& item(available.list[j]);
      RimeSchemaInfo* info = (RimeSchemaInfo*)item.reserved;
      if (!strcmp(item.schema_id, schema_id) &&
          recruited.find(info) == recruited.end()) {
        recruited.insert(info);
        std::wstring itemwstr = u8tow(item.name);
        schema_list_.AddItem(k, 0, itemwstr.c_str());
        schema_list_.SetItemData(k, (DWORD_PTR)info);
        schema_list_.SetCheckState(k, TRUE);
        ++k;
        break;
      }
    }
  }
  for (size_t i = 0; i < available.size; ++i) {
    RimeSchemaListItem& item(available.list[i]);
    RimeSchemaInfo* info = (RimeSchemaInfo*)item.reserved;
    if (recruited.find(info) == recruited.end()) {
      recruited.insert(info);
      std::wstring itemwstr = u8tow(item.name);
      schema_list_.AddItem(k, 0, itemwstr.c_str());
      schema_list_.SetItemData(k, (DWORD_PTR)info);
      ++k;
    }
  }
  auto hotkeys_str = api_->get_hotkeys(settings_);
  if (hotkeys_str) {
    std::wstring txt = u8tow(hotkeys_str);
    hotkeys_.SetWindowTextW(txt.c_str());
  }
  loaded_ = true;
  modified_ = false;
}

void SwitcherSettingsDialog::LoadKeyboardLayout() {
  DWORD layout = 0;
  DWORD size = sizeof(layout);
  if (RegGetValueW(HKEY_CURRENT_USER, kWeaselRegKey, kKeyboardLayoutValue,
                   RRF_RT_REG_DWORD, nullptr, &layout,
                   &size) != ERROR_SUCCESS) {
    layout = 0;
  }
  keyboard_layout_.SetCurSel(layout == 1 ? 1 : 0);
}

void SwitcherSettingsDialog::SaveKeyboardLayout() {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kWeaselRegKey, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  DWORD layout = keyboard_layout_.GetCurSel() == 1 ? 1 : 0;
  RegSetValueExW(key, kKeyboardLayoutValue, 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&layout), sizeof(layout));
  RegCloseKey(key);
}

void SwitcherSettingsDialog::ShowDetails(RimeSchemaInfo* info) {
  if (!info)
    return;
  std::string details;
  if (const char* name = api_->get_schema_name(info)) {
    details += name;
  }
  if (const char* author = api_->get_schema_author(info)) {
    (details += "\n\n") += author;
  }
  if (const char* description = api_->get_schema_description(info)) {
    (details += "\n\n") += description;
  }
  std::wstring txt = u8tow(details.c_str());
  description_.SetWindowTextW(txt.c_str());
}

LRESULT SwitcherSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  schema_list_.SubclassWindow(GetDlgItem(IDC_SCHEMA_LIST));
  schema_list_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT,
                                        LVS_EX_FULLROWSELECT);

  CString schema_name;
  schema_name.LoadStringW(IDS_STR_SCHEMA_NAME);
  schema_list_.AddColumn(schema_name, 0);
  CRect rc;
  schema_list_.GetClientRect(&rc);
  schema_list_.SetColumnWidth(0, rc.Width() - 20);

  description_.Attach(GetDlgItem(IDC_SCHEMA_DESCRIPTION));

  hotkeys_.Attach(GetDlgItem(IDC_HOTKEYS));
  hotkeys_.EnableWindow(FALSE);

  keyboard_layout_.Attach(GetDlgItem(IDC_KEYBOARD_LAYOUT));
  keyboard_layout_.AddString(L"跟随 Windows");
  keyboard_layout_.AddString(L"日语键盘 (106/109)");
  LoadKeyboardLayout();

  Populate();

  CenterWindow();
  BringWindowToTop();
  return TRUE;
}

LRESULT SwitcherSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnOK(WORD, WORD code, HWND, BOOL&) {
  SaveKeyboardLayout();
  if (modified_ && settings_ && schema_list_.GetItemCount() != 0) {
    const char** selection = new const char*[schema_list_.GetItemCount()];
    int count = 0;
    for (int i = 0; i < schema_list_.GetItemCount(); ++i) {
      if (!schema_list_.GetCheckState(i))
        continue;
      RimeSchemaInfo* info = (RimeSchemaInfo*)(schema_list_.GetItemData(i));
      if (info) {
        selection[count++] = api_->get_schema_id(info);
      }
    }
    if (count == 0) {
      // MessageBox(_T("至少要選用一項吧。"), _T("小狼毫不是這般用法"), MB_OK |
      // MB_ICONEXCLAMATION);
      MSG_BY_IDS(IDS_STR_ERR_AT_LEAST_ONE_SEL, IDS_STR_NOT_REGULAR,
                 MB_OK | MB_ICONEXCLAMATION);
      delete selection;
      return 0;
    }
    api_->select_schemas(settings_, selection, count);
    delete selection;
  }
  EndDialog(code);
  return 0;
}

LRESULT SwitcherSettingsDialog::OnSchemaListItemChanged(int, LPNMHDR p, BOOL&) {
  LPNMLISTVIEW lv = reinterpret_cast<LPNMLISTVIEW>(p);
  if (!loaded_ || !lv || lv->iItem < 0 ||
      lv->iItem >= schema_list_.GetItemCount())
    return 0;
  if ((lv->uNewState & LVIS_STATEIMAGEMASK) !=
      (lv->uOldState & LVIS_STATEIMAGEMASK)) {
    modified_ = true;
  } else if ((lv->uNewState & LVIS_SELECTED) &&
             !(lv->uOldState & LVIS_SELECTED)) {
    ShowDetails((RimeSchemaInfo*)(schema_list_.GetItemData(lv->iItem)));
  }
  return 0;
}
