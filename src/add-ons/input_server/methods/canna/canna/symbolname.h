/* Copyright 1992 NEC Corporation, Tokyo, Japan.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of NEC
 * Corporation not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.  NEC Corporation makes no representations about the
 * suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * NEC CORPORATION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN 
 * NO EVENT SHALL NEC CORPORATION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF 
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR 
 * OTHER TORTUOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR 
 * PERFORMANCE OF THIS SOFTWARE. 
 */

/* $Id: symbolname.h,v 1.1 2004/12/23 22:08:39 korli Exp $
 */

#define S_HenkanNyuuryokuMode	"henkan-nyuuryoku-mode"
#define S_ZenHiraHenkanMode	"zen-hira-henkan-mode"
#define S_HanHiraHenkanMode	"han-hira-henkan-mode"
#define S_ZenKataHenkanMode	"zen-kata-henkan-mode"
#define S_HanKataHenkanMode	"han-kata-henkan-mode"
#define S_ZenAlphaHenkanMode	"zen-alpha-henkan-mode"
#define S_HanAlphaHenkanMode	"han-alpha-henkan-mode"
#define S_ZenHiraKakuteiMode	"zen-hira-kakutei-mode"
#define S_HanHiraKakuteiMode	"han-hira-kakutei-mode"
#define S_ZenKataKakuteiMode	"zen-kata-kakutei-mode"
#define S_HanKataKakuteiMode	"han-kata-kakutei-mode"
#define S_ZenAlphaKakuteiMode	"zen-alpha-kakutei-mode"
#define S_HanAlphaKakuteiMode	"han-alpha-kakutei-mode"
#define S_AlphaMode		"alpha-mode"
#define S_YomiganaiMode		"empty-mode"
#define S_YomiMode		"yomi-mode"
#define S_MojishuMode		"mojishu-mode"
#define S_TankouhoMode		"tankouho-mode"
#define S_IchiranMode		"ichiran-mode"
#define S_ShinshukuMode		"shinshuku-mode"
#define S_HexMode		"hex-mode"
#define S_BushuMode		"bushu-mode"
#define S_YesNoMode		"yes-no-mode"
#define S_OnOffMode		"on-off-mode"
#define S_ExtendMode		"extend-mode"
#define S_RussianMode		"russian-mode"
#define S_GreekMode		"greek-mode"
#define S_LineMode		"line-mode"
#define S_ChangingServerMode	"changing-server-mode"
#define S_HenkanMethodMode	"henkan-method-mode"
#define S_DeleteDicMode		"delete-dic-mode"
#define S_TourokuMode		"touroku-mode"
#define S_TourokuHinshiMode	"touroku-hinshi-mode"
#define S_TourokuDicMode	"touroku-dic-mode"
#define S_QuotedInsertMode	"quoted-insert-mode"
#define S_BubunMuhenkanMode	"bubun-muhenkan-mode"
#define S_MountDicMode		"mount-dic-mode"
#define S_KigouMode		"kigou-mode"
#define S_AutoYomiMode		"chikuji-yomi-mode"
#define S_AutoBunsetsuMode	"chikuji-bunsetsu-mode"
#define S_UnbindKey		"unbind-key-function"
#define S_GUnbindKey		"global-unbind-key-function"
#define S_SetKey		"set-key"
#define S_GSetKey		"global-set-key"
#define S_SetModeDisp		"set-mode-display"
#define S_DefMode		"defmode"
#define S_DefSymbol		"defsymbol"
#define S_DefSelection		"defselection"
#define S_DefMenu		"defmenu"
#define S_SetInitFunc		"initialize-function"
#define S_FN_UseDictionary	"use-dictionary"
#define S_defEscSequence	"define-esc-sequence"
#define S_defXKeysym		"define-x-keysym"

#define S_FN_Undefined		"undefined"
#define S_FN_SelfInsert		"self-insert"
#define S_FN_FunctionalInsert	"self-insert"
#define S_FN_QuotedInsert	"quoted-insert"
#define S_FN_JapaneseMode	"japanese-mode"
#define S_FN_AlphaMode		S_AlphaMode
#define S_FN_HenkanNyuryokuMode	S_HenkanNyuuryokuMode
#define S_FN_ZenHiraKakuteiMode S_ZenHiraKakuteiMode
#define S_FN_ZenKataKakuteiMode	S_ZenKataKakuteiMode
#define S_FN_HanKataKakuteiMode	S_HanKataKakuteiMode
#define S_FN_ZenAlphaKakuteiMode S_ZenAlphaKakuteiMode
#define S_FN_HanAlphaKakuteiMode S_HanAlphaKakuteiMode
#define S_FN_HexMode		S_HexMode
#define S_FN_BushuMode		S_BushuMode
#define S_FN_KigouMode		S_KigouMode
#define S_FN_Forward		"forward"
#define S_FN_Backward		"backward"
#define S_FN_Next		"next"
#define S_FN_Prev		"previous"
#define S_FN_BeginningOfLine	"beginning-of-line"
#define S_FN_EndOfLine		"end-of-line"
#define S_FN_DeleteNext		"delete-next"
#define S_FN_DeletePrevious	"delete-previous"
#define S_FN_KillToEndOfLine	"kill-to-end-of-line"
#define S_FN_Henkan		"henkan"
#define S_FN_HenkanNaive	"henkan-naive"
#define S_FN_HenkanOrSelfInsert	"henkan-or-self-insert"
#define S_FN_HenkanOrDoNothing	"henkan-or-do-nothing"
#define S_FN_Kakutei		"kakutei"
#define S_FN_Extend		"extend"
#define S_FN_Shrink		"shrink"
#define S_FN_AdjustBunsetsu	S_ShinshukuMode
#define S_FN_Quit		"quit"
#define S_FN_ExtendMode		S_ExtendMode
#define S_FN_Touroku		"touroku"
#define S_FN_ConvertAsHex	"convert-as-hex"
#define S_FN_ConvertAsBushu	"convert-as-bushu"
#define S_FN_KouhoIchiran	"kouho-ichiran"
#define S_FN_BubunMuhenkan	"henshu"
#define S_FN_Zenkaku		"zenkaku"
#define S_FN_Hankaku		"hankaku"
#define S_FN_ToUpper		"to-upper"
#define S_FN_Capitalize		"capitalize"
#define S_FN_ToLower		"to-lower"
#define S_FN_Hiragana		"hiragana"
#define S_FN_Katakana		"katakana"
#define S_FN_Romaji		"romaji"
#define S_FN_BaseHiragana	"base-hiragana"
#define S_FN_BaseKatakana	"base-katakana"
#define S_FN_BaseKana		"base-kana"
#define S_FN_BaseEisu		"base-eisu"
#define S_FN_BaseZenkaku	"base-zenkaku"
#define S_FN_BaseHankaku	"base-hankaku"
#define S_FN_BaseKakutei	"base-kakutei"
#define S_FN_BaseHenkan		"base-henkan"
#define S_FN_BaseHiraKataToggle	"base-hiragana-katakana-toggle"
#define S_FN_BaseKanaEisuToggle	"base-kana-eisu-toggle"
#define S_FN_BaseZenHanToggle	"base-zenkaku-hankaku-toggle"
#define S_FN_BaseKakuteiHenkanToggle "base-kakutei-henkan-toggle"
#define S_FN_BaseRotateForward	"base-rotate-forward"
#define S_FN_BaseRotateBackward	"base-rotate-backward"
#define S_FN_Mark		"mark"
#define S_FN_Temporary		"temporary"
#define S_FN_SyncDic		"sync-dictionary"
#define S_FN_FuncSequence	"XXXXXXXXXXXXXX"
#define S_FN_UseOtherKeymap	"XXXXXXXXXXXXXX"
#define S_FN_DefineDicMode	S_TourokuMode
#define S_FN_DeleteDicMode	S_DeleteDicMode
#define S_FN_DicMountMode	"jisho-ichiran"
#define S_FN_EnterChikujiMode	"chikuji-mode"
#define S_FN_EnterRenbunMode	"renbun-mode"
#define S_FN_DisconnectServer	"disconnect-server"
#define S_FN_ChangeServerMode	"switch-server"
#define S_FN_ShowServer		"show-server-name"
#define S_FN_ShowGakushu	"show-gakushu"
#define S_FN_ShowVersion	"show-canna-version"
#define S_FN_ShowPhonogramFile	"show-romkana-table"
#define S_FN_ShowCannaFile	"show-canna-file"
#define S_FN_KanaRotate		"kana-rotate-forward"
#define S_FN_RomajiRotate	"romaji-rotate-forward"
#define S_FN_CaseRotate		"case-rotate-forward"

#define S_VA_RomkanaTable	"romkana-table"
#define S_VA_EnglishTable	"english-table"
#define S_VA_InitMode		"initial-mode"
#define S_VA_CursorWrap		"cursor-wrap"
#define S_VA_SelectDirect	"select-direct"
#define S_VA_NumericalKeySelect	"numerical-key-select"
#define S_VA_BunsetsuKugiri	"bunsetsu-kugiri"
#define S_VA_CharacterBasedMove	"character-based-move"
#define S_VA_ReverseWidely	"reverse-widely"
#define S_VA_ReverseWord	"reverse-word"
#define S_VA_Gakushu		"gakushu"
#define S_VA_QuitIfEOIchiran	"quit-if-end-of-ichiran"
#define S_VA_KakuteiIfEOBunsetsu	"kakutei-if-end-of-bunsetsu"
#define S_VA_StayAfterValidate	"stay-after-validate"
#define S_VA_BreakIntoRoman	"break-into-roman"
#define S_VA_NHenkanForIchiran	"n-henkan-for-ichiran"
#define	S_VA_nKouhoBunsetsu	"n-kouho-bunsetsu"
#define S_VA_keepCursorPosition	"keep-cursor-position"
#define S_VA_GrammaticalQuestion	"grammatical-question"
#define S_VA_ForceKana		"force-kana"
#define S_VA_KouhoCount		"kouho-count"
#define S_VA_Auto		"auto"
#define S_VA_LearnNumericalType "learn-numerical-type"
#define S_VA_BackspaceBehavesAsQuit	"backspace-behaves-as-quit"
#define S_VA_InhibitListCallback	"inhibit-list-callback"
#define S_VA_CannaVersion	"canna-version"
#define S_VA_ProtocolVersion	"protocol-version"
#define S_VA_ServerVersion	"server-version"
#define S_VA_ServerName		"server-name"
#define S_VA_Abandon		"abandon-illegal-phonogram"
#define S_VA_HexDirect		"hex-direct"
#define S_VA_Kojin		"kojin"
#define S_VA_IndexHankaku	"index-hankaku"
#define S_VA_IndexSeparator	"index-separator"
#define S_VA_AllowNextInput	"allow-next-input"
#define S_VA_KeepCursorPosition	"keep-cursor"
#define S_VA_ChikujiContinue	"chikuji-continue"
#define S_VA_RenbunContinue	"renbun-continue"
#define S_VA_MojishuContinue	"mojishu-continue"
#define S_VA_ChikujiRealBackspace "chikuji-force-backspace"
#define S_VA_doKatakanaGakushu  "katakana-touroku"
#define S_VA_doHiraganaGakushu  "hiragana-touroku"
#define S_VA_chikuji_debug      "chikuji-debug"
#define S_VA_nDisconnectServer  "n-keys-to-disconnect"
#define S_VA_ignoreCase		"ignore-case"
#define S_VA_RomajiYuusen	"romaji-yuusen"
#define S_VA_CannaDir		"canna-directory"
#define S_VA_AutoSync		"auto-sync"
#define S_VA_QuicklyEscape	"quickly-escape-from-kigo-input"
#define S_VA_InhibitHanKana	"inhibit-hankaku-kana"
#define S_VA_CodeInput          "code-input"

#define S_IF_HenkanNyuryoku     "(japanese-mode)"
#define S_IF_ZenHiraKakutei     "(japanese-mode base-kakutei)"
#define S_IF_ZenKataKakutei     "(japanese-mode base-kakutei base-katakana)"
#define S_IF_ZenAlphaKakutei    "(japanese-mode base-kakutei base-eisu base-zenkaku)"
#define S_IF_HanKataKakutei     "(japanese-mode base-kakutei base-katakana base-hankaku)"
#define S_IF_HanAlphaKakutei    "(japanese-mode base-kakutei base-eisu base-hankaku)"
#define S_IF_ZenKataHenkan      "(japanese-mode base-katakana)"
#define S_IF_ZenAlphaHenkan     "(japanese-mode base-eisu base-zenkaku)"
#define S_IF_HanKataHenkan      "(japanese-mode base-katakana base-hankaku)"
#define S_IF_HanAlphaHenkan     "(japanese-mode hase-eisu base-hankaku)"

#define S_FN_PageUp		"page-up"
#define S_FN_PageDown		"page-down"
#define S_FN_Edit		"edit"
#define S_FN_BubunKakutei	"bubun-kakutei"
#define S_FN_HenkanRegion	"henkan-region"
#define S_FN_PhonoEdit		"phono-edit"
#define S_FN_DicEdit		"dic-edit"
#define S_FN_Configure		"configure"
