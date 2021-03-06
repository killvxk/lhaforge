/*
 * Copyright (c) 2005-2012, Claybird
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:

 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Claybird nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "stdafx.h"
#include "FileTreeView.h"
#include "../resource.h"
#include "../Dialogs/LogDialog.h"
#include "../Utilities/StringUtil.h"
#include "../Utilities/OSUtil.h"


CFileTreeView::CFileTreeView(CFileListModel &rModel):
	m_bSelfAction(false),
	m_hFrameWnd(NULL),
	m_DropTarget(this),
	m_hDropHilight(NULL),
	mr_Model(rModel)
{
}

LRESULT CFileTreeView::OnCreate(LPCREATESTRUCT lpcs)
{
	LRESULT lRes=DefWindowProc();
	SetFont(AtlGetDefaultGuiFont());

	//モデルにイベントハンドラ登録
	mr_Model.addEventListener(m_hWnd);

	//イメージリスト作成
	m_ImageList.Create(::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CXSMICON), ILC_COLOR32 | ILC_MASK, 8, 1);
	SetImageList(m_ImageList,TVSIL_NORMAL);
	//フォルダアイコン取得
	SHFILEINFO shfi;
	SHGetFileInfo(_T("dummy"),FILE_ATTRIBUTE_DIRECTORY,&shfi,sizeof(shfi),SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_SMALLICON);
	m_ImageList.AddIcon(shfi.hIcon);
	DestroyIcon(shfi.hIcon);
	SHGetFileInfo(_T("dummy"),FILE_ATTRIBUTE_DIRECTORY,&shfi,sizeof(shfi),SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_SMALLICON|SHGFI_OPENICON);
	m_ImageList.AddIcon(shfi.hIcon);
	DestroyIcon(shfi.hIcon);

	return lRes;
}

LRESULT CFileTreeView::OnDestroy()
{
	mr_Model.removeEventListener(m_hWnd);
	m_ImageList.Destroy();
	return 0;
}

LRESULT CFileTreeView::OnFileListArchiveLoaded(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	Clear();
	ConstructTree();

	//ファイルドロップの受け入れ
	EnableDropTarget(true);
	return 0;
}


LRESULT CFileTreeView::OnFileListNewContent(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	UpdateCurrentNode();
	return 0;
}

LRESULT CFileTreeView::OnFileListUpdated(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	//nothing to do
	return 0;
}

bool CFileTreeView::UpdateCurrentNode()
{
	ARCHIVE_ENTRY_INFO_TREE* lpNode=mr_Model.GetCurrentNode();
	ITEMDICT::iterator ite=m_TreeItemMap.find(lpNode);
	if(m_TreeItemMap.end()==ite){
		return false;
	}
	EnsureVisible((*ite).second);
	SelectItem((*ite).second);
	return true;
}


bool CFileTreeView::ConstructTree(HTREEITEM hParentItem,ARCHIVE_ENTRY_INFO_TREE* lpNode)
{
	BeginSelfAction();
	HTREEITEM hItem;
	if(NULL==hParentItem){
		lpNode=mr_Model.GetRootNode();
		hItem=InsertItem(PathFindFileName(mr_Model.GetArchiveFileName()), TVI_ROOT, TVI_LAST);
		SetItemImage(hItem,2,2);

		//アーカイブアイコンの設定
		m_ImageList.Remove(2);	//古いアイコンを削除

		//関連付けのアルアイコンを取得
		SHFILEINFO shfi;
		::SHGetFileInfo(mr_Model.GetArchiveFileName(), 0, &shfi, sizeof(shfi),SHGFI_ICON | SHGFI_SMALLICON);
		m_ImageList.AddIcon(shfi.hIcon);
	}else{
		//子を追加
		hItem=InsertItem(lpNode->strTitle,hParentItem,TVI_LAST);
		SetItemImage(hItem,0,1);
	}
	m_TreeItemMap.insert(ITEMDICT::value_type(lpNode,hItem));
	//アイテムのデータにノードのポインタを設定
	SetItemData(hItem,(DWORD_PTR)lpNode);

	//Node配下のフォルダに対して処理
	UINT numItems=lpNode->GetNumChildren();
	for(UINT i=0;i<numItems;i++){
		ARCHIVE_ENTRY_INFO_TREE* lpChild=lpNode->GetChild(i);
		if(lpChild->bDir){
			//ディレクトリなら追加
			ConstructTree(hItem,lpChild);
		}
	}
	return true;
}

void CFileTreeView::Clear()
{
	DeleteAllItems();
	m_TreeItemMap.clear();
}

void CFileTreeView::ExpandTree()
{
	ITEMDICT::iterator ite=m_TreeItemMap.begin();
	ITEMDICT::iterator end=m_TreeItemMap.end();
	for(;ite!=end;++ite){
		Expand((*ite).second);
	}
}

LRESULT CFileTreeView::OnTreeSelect(LPNMHDR)
{
	TRACE(__FUNCTIONW__ _T("\n"));
	if(IsSelfAction()){
		//ConstructTree()により自分が選んだ時、自分で反応してしまわないためのフラグ
		EndSelfAction();
	}else{
		//選択されたアイテムに関連付けられたノードを取得し、
		//その配下のファイルをリストビューに表示する
		ARCHIVE_ENTRY_INFO_TREE* lpCurrent=mr_Model.GetCurrentNode();

		HTREEITEM hItem=GetSelectedItem();
		if(!hItem)return 0;

		ARCHIVE_ENTRY_INFO_TREE* lpNode=(ARCHIVE_ENTRY_INFO_TREE*)GetItemData(hItem);
		ASSERT(lpNode);

		//TODO
		//if(!m_FileListModel.IsRoot())m_FileListModel.GetRootNode().EndFindItem();
		if(lpNode && lpNode!=lpCurrent){
			mr_Model.SetCurrentNode(lpNode);
		}
	}
	return 0;
}


void CFileTreeView::EnableDropTarget(bool bEnable)
{
	if(bEnable){
		//ドロップ受け入れ設定
		::RegisterDragDrop(m_hWnd,&m_DropTarget);
	}else{
		//ドロップを受け入れない
		::RevokeDragDrop(m_hWnd);
	}
}

//---------------------------------------------------------
//    IDropCommunicatorの実装:ドラッグ&ドロップによる圧縮
//---------------------------------------------------------
HRESULT CFileTreeView::DragEnter(IDataObject *lpDataObject,POINTL &pt,DWORD &dwEffect)
{
	return DragOver(lpDataObject,pt,dwEffect);
}

HRESULT CFileTreeView::DragLeave()
{
	//全てのハイライトを無効に
	if(m_hDropHilight){
		SetItemState( m_hDropHilight, ~LVIS_DROPHILITED, LVIS_DROPHILITED);
	}
	m_hDropHilight=NULL;
	return S_OK;
}

HRESULT CFileTreeView::DragOver(IDataObject *,POINTL &pt,DWORD &dwEffect)
{
	//フォーマットに対応した処理をする
	if(!m_DropTarget.QueryFormat(CF_HDROP) || !mr_Model.IsAddItemsSupported()){	//ファイル専用
		//ファイルではないので拒否
		dwEffect = DROPEFFECT_NONE;
	}else{
		//---ドロップ先アイテムを取得
		CPoint ptTemp(pt.x,pt.y);
		ScreenToClient(&ptTemp);
		HTREEITEM hItem=HitTest(ptTemp,NULL);

		//---ちらつきを押さえるため、前と同じアイテムがハイライトされるならハイライトをクリアしない
		if(hItem!=m_hDropHilight){
			//前のハイライトを無効に
			if(m_hDropHilight){
				SetItemState( m_hDropHilight, ~LVIS_DROPHILITED, LVIS_DROPHILITED);
			}
			//新しいハイライト
			if(hItem){
				SelectDropTarget(hItem);
				m_hDropHilight=hItem;
			}
		}
		dwEffect = hItem ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	}
	return S_OK;
}

//ファイルのドロップ
HRESULT CFileTreeView::Drop(IDataObject *lpDataObject,POINTL &pt,DWORD &dwEffect)
{
	//前のハイライトを無効に
	if(m_hDropHilight){
		SetItemState( m_hDropHilight, ~LVIS_DROPHILITED, LVIS_DROPHILITED);
	}else{
		return E_HANDLE;
	}

	//ファイル取得
	std::list<CString> fileList;
	if(S_OK==m_DropTarget.GetDroppedFiles(lpDataObject,fileList)){
		//ファイルがドロップされた
		dwEffect = DROPEFFECT_COPY;

		//---ドロップ先を特定
		CPoint ptTemp(pt.x,pt.y);
		ScreenToClient(&ptTemp);
		HTREEITEM hItem=HitTest(ptTemp,NULL);

		CString strDest;	//放り込む先
		ARCHIVE_ENTRY_INFO_TREE* lpNode=(ARCHIVE_ENTRY_INFO_TREE*)GetItemData(hItem);
		if(lpNode){		//アイテム上にDnD
			//アイテムがフォルダだったらそのフォルダに追加
			ASSERT(lpNode->bDir);
			ArcEntryInfoTree_GetNodePathRelative(lpNode,mr_Model.GetRootNode(),strDest);
		}else{
			return E_HANDLE;
		}
		TRACE(_T("Target:%s\n"),(LPCTSTR)strDest);

		//追加開始
		::EnableWindow(m_hFrameWnd,FALSE);
		CString strLog;

		HRESULT hr=mr_Model.AddItem(fileList,strDest,strLog);

		::EnableWindow(m_hFrameWnd,TRUE);
		::SetForegroundWindow(m_hFrameWnd);

		if(FAILED(hr) || S_FALSE==hr){
			CString msg;
			switch(hr){
			case E_LF_SAME_INPUT_AND_OUTPUT:	//アーカイブ自身を追加しようとした
				msg.Format(IDS_ERROR_SAME_INPUT_AND_OUTPUT,mr_Model.GetArchiveFileName());
				ErrorMessage(msg);
				break;
			case E_LF_UNICODE_NOT_SUPPORTED:	//ファイル名にUNICODE文字を持つファイルを圧縮しようとした
				ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_UNICODEPATH)));
				break;
			case S_FALSE:	//追加処理に問題
				{
					CLogDialog LogDialog;
					LogDialog.SetData(strLog);
					LogDialog.DoModal();
				}
				break;
			default:
				ASSERT(!"This code cannot be run");
			}
			return E_INVALIDARG;
		}
		//アーカイブ更新
		::PostMessage(m_hFrameWnd,WM_FILELIST_REFRESH,0,0);
		return S_OK;
	}else{
		//受け入れできない形式
		dwEffect = DROPEFFECT_NONE;
		return S_FALSE;	//S_OK
	}
}

LRESULT CFileTreeView::OnRClick(LPNMHDR lpNM)
{
	CPoint pt;
	GetCursorPos(&pt);
	CPoint ptClient=pt;
	ScreenToClient(&ptClient);
	SelectItem(HitTest(ptClient,NULL));
	OnContextMenu(m_hWnd,pt);
	return 0;
}


//コンテキストメニューを開く
void CFileTreeView::OnContextMenu(HWND hWndCtrl,CPoint &Point)
{
	//選択されたアイテムを列挙
	std::list<ARCHIVE_ENTRY_INFO_TREE*> items;
	GetSelectedItems(items);

	//何も選択していない、もしくはルートを選択した場合は表示しない
	if(items.empty())return;

	if(-1==Point.x&&-1==Point.y){
		//キーボードからの入力である場合
		//リストビューの左上に表示する
		Point.x=Point.y=0;
		ClientToScreen(&Point);
	}

	CMenu cMenu;
	CMenuHandle cSubMenu;
	HWND hWndSendTo=NULL;
	if((*items.begin())->lpParent==NULL){
		hWndSendTo=m_hFrameWnd;
		//ルートメニュー
		cMenu.LoadMenu(IDR_ARCHIVE_POPUP);
		cSubMenu=cMenu.GetSubMenu(0);
	}else{
		hWndSendTo=m_hWnd;
		//アイテムメニュー
		//部分解凍が使用できないなら、メニューを表示する意味がない
		//TODO:削除メニューはどうする?部分解凍できずに削除可能はほぼあり得ない
		if(!mr_Model.IsExtractEachSupported()){
			MessageBeep(MB_ICONASTERISK);
			return;
		}

		//---右クリックメニュー表示
		cMenu.LoadMenu(IDR_FILELIST_POPUP);
		cSubMenu=cMenu.GetSubMenu(0);

		//コマンドを追加するためのサブメニューを探す
		int MenuCount=cSubMenu.GetMenuItemCount();
		int iIndex=-1;
		for(int i=0;i<=MenuCount;i++){
			if(-1==cSubMenu.GetMenuItemID(i)){	//ポップアップの親
				iIndex=i;
				break;
			}
		}
		ASSERT(-1!=iIndex);
		if(-1!=iIndex){
			MenuCommand_MakeUserAppMenu(cSubMenu.GetSubMenu(iIndex));
			MenuCommand_MakeSendToMenu(cSubMenu.GetSubMenu(iIndex+1));
		}
	}
	cSubMenu.TrackPopupMenu(TPM_LEFTALIGN|TPM_RIGHTBUTTON,Point.x, Point.y, hWndSendTo,NULL);
}

void CFileTreeView::OnDelete(UINT uNotifyCode,int nID,HWND hWndCtrl)
{
	if(!mr_Model.IsDeleteItemsSupported()){
		//選択ファイルの削除はサポートされていない
		if(1==uNotifyCode){	//アクセラレータから操作
			MessageBeep(MB_OK);
		}else{
			ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_FILELIST_DELETE_SELECTED_NOT_SUPPORTED)));
		}
		return;// false;
	}

	//選択されたファイルを列挙
	std::list<ARCHIVE_ENTRY_INFO_TREE*> items;
	GetSelectedItems(items);

	//ファイルが選択されていなければエラー
	ASSERT(!items.empty());
	if(items.empty()){
		return;// false;
	}

	//消去確認
	if(IDYES!=MessageBox(CString(MAKEINTRESOURCE(IDS_ASK_FILELIST_DELETE_SELECTED)),UtilGetMessageCaption(),MB_YESNO|MB_DEFBUTTON2|MB_ICONEXCLAMATION)){
		return;
	}

	//----------------
	// ファイルを処理
	//----------------
	//ウィンドウを使用不可に
	::EnableWindow(m_hFrameWnd,FALSE);
	CString strLog;
	bool bRet=mr_Model.DeleteItems(items,strLog);
	//ウィンドウを使用可能に
	::EnableWindow(m_hFrameWnd,TRUE);
	SetForegroundWindow(m_hFrameWnd);
	if(!bRet){
		CLogDialog LogDlg;
		LogDlg.SetData(strLog);
		LogDlg.DoModal(m_hFrameWnd);
	}

	//アーカイブ内容更新
	::PostMessage(m_hFrameWnd,WM_FILELIST_REFRESH,NULL,NULL);
}


void CFileTreeView::OnOpenWithUserApp(UINT uNotifyCode,int nID,HWND hWndCtrl)
{
	if(nID<ID_MENUITEM_USERAPP_END){
		//LhaForge設定のコマンド
		OnUserApp(MenuCommand_GetCmdArray(),nID-ID_MENUITEM_USERAPP_BEGIN);
	}else{
		//SendToのコマンド
		OnSendToApp(nID-ID_MENUITEM_USERAPP_END);
	}
}


bool CFileTreeView::OnUserApp(const std::vector<CMenuCommandItem> &menuCommandArray,UINT nID)	//「プログラムで開く」のハンドラ
{
	ASSERT(!menuCommandArray.empty());
	ASSERT(nID<menuCommandArray.size());
	if(nID>=menuCommandArray.size())return false;

	if(!mr_Model.IsOK())return false;

	//---選択解凍開始
	//選択されたアイテムを列挙
	std::list<ARCHIVE_ENTRY_INFO_TREE*> items;
	GetSelectedItems(items);

	std::list<CString> filesList;
	if(!items.empty()){
		CString strLog;
		if(!mr_Model.MakeSureItemsExtracted(NULL,mr_Model.GetRootNode(),items,filesList,false,strLog)){
			CLogDialog LogDialog;
			LogDialog.SetData(strLog);
			LogDialog.DoModal();
			return false;
		}
	}

	//---実行情報取得
	//パラメータ展開に必要な情報
	std::map<stdString,CString> envInfo;
	UtilMakeExpandInformation(envInfo);

	//コマンド・パラメータ展開
	CString strCmd,strParam,strDir;
	UtilExpandTemplateString(strCmd,  menuCommandArray[nID].Path, envInfo);	//コマンド
	UtilExpandTemplateString(strParam,menuCommandArray[nID].Param,envInfo);	//パラメータ
	UtilExpandTemplateString(strDir,  menuCommandArray[nID].Dir,  envInfo);	//ディレクトリ

	//引数置換
	if(-1!=strParam.Find(_T("%F"))){
		//ファイル一覧を連結して作成
		CString strFileList;
		for(std::list<CString>::iterator ite=filesList.begin();ite!=filesList.end();++ite){
			CPath path=*ite;
			path.QuoteSpaces();
			strFileList+=(LPCTSTR)path;
			strFileList+=_T(" ");
		}
		strParam.Replace(_T("%F"),strFileList);
		//---実行
		::ShellExecute(GetDesktopWindow(),NULL,strCmd,strParam,strDir,SW_SHOW);
	}else if(-1!=strParam.Find(_T("%S"))){
		for(std::list<CString>::iterator ite=filesList.begin();ite!=filesList.end();++ite){
			CPath path=*ite;
			path.QuoteSpaces();

			CString strParamTmp=strParam;
			strParamTmp.Replace(_T("%S"),(LPCTSTR)path);
			//---実行
			::ShellExecute(GetDesktopWindow(),NULL,strCmd,strParamTmp,strDir,SW_SHOW);
		}
	}else{
		::ShellExecute(GetDesktopWindow(),NULL,strCmd,strParam,strDir,SW_SHOW);
	}

	return true;
}

bool CFileTreeView::OnSendToApp(UINT nID)	//「プログラムで開く」のハンドラ
{
	ASSERT(MenuCommand_GetNumSendToCmd());
	ASSERT(nID<MenuCommand_GetNumSendToCmd());
	if(nID>=MenuCommand_GetNumSendToCmd())return false;

	if(!mr_Model.IsOK())return false;

	//---選択解凍開始
	//選択されたアイテムを列挙
	std::list<ARCHIVE_ENTRY_INFO_TREE*> items;
	GetSelectedItems(items);

	std::list<CString> filesList;
	if(!items.empty()){
		CString strLog;
		if(!mr_Model.MakeSureItemsExtracted(NULL,mr_Model.GetRootNode(),items,filesList,false,strLog)){
			CLogDialog LogDialog;
			LogDialog.SetData(strLog);
			LogDialog.DoModal();
			return false;
		}
	}

	//引数置換
	const std::vector<SHORTCUTINFO>& sendToCmd=MenuCommand_GetSendToCmdArray();
	if(PathIsDirectory(sendToCmd[nID].strCmd)){
		//対象はディレクトリなので、コピー
		CString strFiles;
		for(std::list<CString>::const_iterator ite=filesList.begin();ite!=filesList.end();++ite){
			CPath file=*ite;
			file.RemoveBackslash();
			strFiles+=file;
			strFiles+=_T('|');
		}
		strFiles+=_T('|');
		//TRACE(strFiles);
		std::vector<TCHAR> srcBuf(strFiles.GetLength()+1);
		UtilMakeFilterString(strFiles,&srcBuf[0],srcBuf.size());

		CPath destDir=sendToCmd[nID].strCmd;
		destDir.AddBackslash();
		//Windows標準のコピー動作
		SHFILEOPSTRUCT fileOp={0};
		fileOp.wFunc=FO_COPY;
		fileOp.fFlags=FOF_NOCONFIRMMKDIR|FOF_NOCOPYSECURITYATTRIBS|FOF_NO_CONNECTED_ELEMENTS;
		fileOp.pFrom=&srcBuf[0];
		fileOp.pTo=destDir;

		//コピー実行
		if(::SHFileOperation(&fileOp)){
			//エラー
			ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_FILE_COPY)));
			return false;
		}else if(fileOp.fAnyOperationsAborted){
			//キャンセル
			ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_USERCANCEL)));
			return false;
		}
		return true;
	}else{
		//送り先はプログラム
		//ファイル一覧を連結して作成
		CString strFileList;
		for(std::list<CString>::iterator ite=filesList.begin();ite!=filesList.end();++ite){
			CPath path=*ite;
			path.QuoteSpaces();
			strFileList+=(LPCTSTR)path;
			strFileList+=_T(" ");
		}
		CString strParam=sendToCmd[nID].strParam+_T(" ")+strFileList;
		//---実行
		CPath cmd=sendToCmd[nID].strCmd;
		cmd.QuoteSpaces();
		CPath workDir=sendToCmd[nID].strWorkingDir;
		workDir.QuoteSpaces();
		::ShellExecute(GetDesktopWindow(),NULL,cmd,strParam,workDir,SW_SHOW);
	}

	return true;
}

void CFileTreeView::OnExtractItem(UINT,int nID,HWND)
{
	//アーカイブと同じフォルダに解凍する場合はtrue
	const bool bSameDir=(ID_MENUITEM_EXTRACT_SELECTED_SAMEDIR==nID);

	if(!mr_Model.IsOK()){
		return;// false;
	}
	if(!mr_Model.IsExtractEachSupported()){
		//選択ファイルの解凍はサポートされていない
		ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_FILELIST_EXTRACT_SELECTED_NOT_SUPPORTED)));
		return;// false;
	}

	//選択されたアイテムを列挙
	std::list<ARCHIVE_ENTRY_INFO_TREE*> items;
	GetSelectedItems(items);
	if(items.empty()){
		//選択されたファイルがない
		ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_FILELIST_NOT_SELECTED)));
		return;// false;
	}

	//解凍
	CString strLog;
	HRESULT hr=mr_Model.ExtractItems(m_hFrameWnd,bSameDir,items,(*items.begin())->lpParent,strLog);

	SetForegroundWindow(m_hFrameWnd);

	if(FAILED(hr)){
		CLogDialog LogDialog;
		LogDialog.SetData(strLog);
		LogDialog.DoModal();
	}
}

void CFileTreeView::GetSelectedItems(std::list<ARCHIVE_ENTRY_INFO_TREE*> &items)
{
	items.clear();
	HTREEITEM hItem=GetSelectedItem();
	if(hItem){
		ARCHIVE_ENTRY_INFO_TREE* lpNode=(ARCHIVE_ENTRY_INFO_TREE*)GetItemData(hItem);
		items.push_back(lpNode);
	}
	ASSERT(items.size()<=1);
}

void CFileTreeView::OnOpenAssociation(UINT uNotifyCode,int nID,HWND hWndCtrl)
{
	//trueなら存在するテンポラリファイルを削除してから解凍する
	const bool bOverwrite=(nID==ID_MENUITEM_OPEN_ASSOCIATION_OVERWRITE);
	OpenAssociation(bOverwrite,true);
}


void CFileTreeView::OnExtractTemporary(UINT uNotifyCode,int nID,HWND hWndCtrl)
{
	//上書きはするが、開かない
	OpenAssociation(true,false);
}

//bOverwrite:trueなら存在するテンポラリファイルを削除してから解凍する
bool CFileTreeView::OpenAssociation(bool bOverwrite,bool bOpen)
{
	if(!mr_Model.IsExtractEachSupported()){
		//選択ファイルの解凍はサポートされていない
		ErrorMessage(CString(MAKEINTRESOURCE(IDS_ERROR_FILELIST_EXTRACT_SELECTED_NOT_SUPPORTED)));
		return false;
	}

	if(!mr_Model.CheckArchiveExists()){	//存在しないならエラー
		CString msg;
		msg.Format(IDS_ERROR_FILE_NOT_FOUND,mr_Model.GetArchiveFileName());
		ErrorMessage(msg);
		return false;
	}

	//選択されたアイテムを列挙
	std::list<ARCHIVE_ENTRY_INFO_TREE*> items;
	GetSelectedItems(items);

	if(!items.empty()){
		std::list<CString> filesList;
		CString strLog;
		if(!mr_Model.MakeSureItemsExtracted(NULL,mr_Model.GetRootNode(),items,filesList,bOverwrite,strLog)){
			CLogDialog LogDialog;
			LogDialog.SetData(strLog);
			LogDialog.DoModal();
		}
		if(bOpen)OpenAssociation(filesList);
	}

	return true;
}

void CFileTreeView::OpenAssociation(const std::list<CString> &filesList)
{
	for(std::list<CString>::const_iterator ite=filesList.begin();ite!=filesList.end();++ite){
		//拒否されたら上書きも追加解凍もしない;ディレクトリなら拒否のみチェック
		bool bDenyOnly=BOOL2bool(::PathIsDirectory(*ite));//lpNode->bDir;
		if(UtilPathAcceptSpec(*ite,mr_Model.GetOpenAssocExtDeny(),mr_Model.GetOpenAssocExtAccept(),bDenyOnly)){
			::ShellExecute(GetDesktopWindow(),NULL,*ite,NULL,NULL,SW_SHOW);
			TRACE(_T("%s\n"),(LPCTSTR)*ite);
			//::ShellExecute(GetDesktopWindow(),_T("explore"),*ite,NULL,NULL,SW_SHOW);
		}else{
			::MessageBeep(MB_ICONEXCLAMATION);
		}
	}
}
