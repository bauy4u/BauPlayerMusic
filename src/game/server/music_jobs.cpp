/* Music backend job implementations. */
#include "gamecontext.h"
#include "music_config.h"

#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <utility>

namespace
{

const char *JsonStringField(const json_value *pObject, const char *pName)
{
	const json_value *pValue = json_object_get(pObject, pName);
	if(pValue->type != json_string)
		return nullptr;
	return json_string_get(pValue);
}

bool JsonBoolField(const json_value *pObject, const char *pName, bool Default = false)
{
	const json_value *pValue = json_object_get(pObject, pName);
	if(pValue->type == json_boolean)
		return json_boolean_get(pValue) != 0;
	return Default;
}

bool JsonFloatField(const json_value *pObject, const char *pName, float *pValue)
{
	const json_value *pJsonValue = json_object_get(pObject, pName);
	if(pJsonValue->type == json_double)
	{
		*pValue = (float)pJsonValue->u.dbl;
		return true;
	}
	if(pJsonValue->type == json_integer)
	{
		*pValue = (float)pJsonValue->u.integer;
		return true;
	}
	return false;
}

bool JsonIntField(const json_value *pObject, const char *pName, int64_t *pValue)
{
	const json_value *pJsonValue = json_object_get(pObject, pName);
	if(pJsonValue->type == json_integer)
	{
		*pValue = pJsonValue->u.integer;
		return true;
	}
	return false;
}

std::string JsonErrorMessage(const json_value *pObject, const char *pFallback)
{
	const char *pMessage = JsonStringField(pObject, "message");
	if(!pMessage)
		pMessage = JsonStringField(pObject, "error");
	return pMessage ? std::string(pMessage) : std::string(pFallback);
}

}

void CSongSearchJob::Run()  
{  
    m_pRequest->Wait();  
      
    if(m_pRequest->State() != EHttpState::DONE)  
    {  
        m_pGameContext->QueueMusicSearchResult(m_ClientID, {}, "搜索失败，请稍后重试", m_ForVoteMenu);  
        return;  
    }  
      
    json_value *pJson = m_pRequest->ResultJson();  
    if(!pJson)  
    {  
        m_pGameContext->QueueMusicSearchResult(m_ClientID, {}, "搜索结果解析失败", m_ForVoteMenu);  
        return;  
    }  
      
    if(pJson->type != json_array)  
    {  
        m_pGameContext->QueueMusicSearchResult(m_ClientID, {}, "搜索结果格式错误", m_ForVoteMenu);  
        json_value_free(pJson);  
        return;  
    }  
      
    std::vector<SongInfo> Results;  
      
    const int ResultLimit = m_ForVoteMenu ? g_Config.m_SvMusicVoteSearchResults : g_Config.m_SvMusicSearchResults;
    for(int i = 0; i < json_array_length(pJson) && i < ResultLimit; i++)
    {  
        const json_value *pSong = json_array_get(pJson, i);
        if(pSong->type != json_object)   
            continue;  
          
        SongInfo Info;  
        const char *pTitle = JsonStringField(pSong, "title");
        const char *pArtist = JsonStringField(pSong, "artist");
        const char *pPageUrl = JsonStringField(pSong, "page_url");
        if(pTitle)
            Info.title = pTitle;
        if(pArtist)
            Info.artist = pArtist;
        if(pPageUrl)
            Info.page_url = pPageUrl;
          
        if(!Info.title.empty() && !Info.artist.empty() && !Info.page_url.empty())
        {  
            Results.push_back(Info);  
        }  
    }  
      
    m_pGameContext->QueueMusicSearchResult(m_ClientID, std::move(Results), nullptr, m_ForVoteMenu);  
    json_value_free(pJson);  
}

CMapUploadJob::CMapUploadJob(CGameContext *pGameContext, std::shared_ptr<CHttpRequest> pRequest) :  
    m_pGameContext(pGameContext), m_pRequest(pRequest) {}  
  
void CMapUploadJob::Run()  
{  
    m_pRequest->Wait();  
  
    if(m_pRequest->State() != EHttpState::DONE)
    {  
        m_pGameContext->QueueMusicUploadResult(false, "upload request failed");  
        return;
    }  

    json_value *pJson = m_pRequest->ResultJson();
    if(!pJson || pJson->type != json_object)
    {
        if(pJson)
            json_value_free(pJson);
        m_pGameContext->QueueMusicUploadResult(false, "upload response JSON was invalid");
        return;
    }

    const bool Success = JsonBoolField(pJson, "success");
    const std::string Error = JsonErrorMessage(pJson, "object storage upload returned an error");
    json_value_free(pJson);

    m_pGameContext->QueueMusicUploadResult(Success, Success ? nullptr : Error.c_str());
}

void CSongPreloadJob::Run()  
{  
	char aEscapedTitle[512];  
	char aEscapedArtist[512];   
	char aEscapedUrl[1024];  
	char aEscapedMapName[256];
	char aEscapedTargetMapPath[IO_MAX_PATH_LENGTH * 2];
  
	EscapeJson(aEscapedTitle, sizeof(aEscapedTitle), m_Song.title.c_str());  
	EscapeJson(aEscapedArtist, sizeof(aEscapedArtist), m_Song.artist.c_str());  
	EscapeJson(aEscapedUrl, sizeof(aEscapedUrl), m_Song.page_url.c_str());  
	EscapeJson(aEscapedMapName, sizeof(aEscapedMapName), m_MapName.c_str());
	EscapeJson(aEscapedTargetMapPath, sizeof(aEscapedTargetMapPath), m_TargetMapPath.c_str());
      
    char aJsonString[2048];  
    str_format(aJsonString, sizeof(aJsonString),   
	               "{"  
	               "\"title\": \"%s\","  
	               "\"artist\": \"%s\","  
	               "\"page_url\": \"%s\","
	               "\"map_name\": \"%s\","
	               "\"target_map_path\": \"%s\""
	               "}",   
	               aEscapedTitle, aEscapedArtist, aEscapedUrl, aEscapedMapName, aEscapedTargetMapPath);  
      
    auto pRequest = std::make_shared<CHttpRequest>(m_DownloadEndpoint.c_str());  
    pRequest->PostJson(aJsonString);  
    pRequest->WriteToMemory();  
    // The backend does not stream progress while downloading, transcoding and
    // patching the map, so a low-speed timeout would abort healthy requests.
    pRequest->Timeout(CTimeout{10000, 180000, 0, 0});
      
    m_pHttp->Run(pRequest);  
      
    pRequest->Wait();  
      
    if(pRequest->State() != EHttpState::DONE)  
    {  
        m_pGameContext->QueueMusicPreloadResult(m_Song, m_QueueIndex, false, 0.0f, m_MapName.c_str(), nullptr, nullptr, "HTTP request failed");  
        return;  
    }  
      
    json_value *pJson = pRequest->ResultJson();  
    if(!pJson || pJson->type != json_object)
    {  
        if(pJson)
            json_value_free(pJson);
        m_pGameContext->QueueMusicPreloadResult(m_Song, m_QueueIndex, false, 0.0f, m_MapName.c_str(), nullptr, nullptr, "JSON parse failed");  
        return;  
    }  
      
    float Duration = 0.0f;  
    const char *pStatus = JsonStringField(pJson, "status");
    const bool Success = pStatus && str_comp(pStatus, "success") == 0;
    JsonFloatField(pJson, "duration", &Duration);
	const char *pPreparedMapPath = JsonStringField(pJson, "prepared_map_path");
	const char *pMapSha256 = JsonStringField(pJson, "map_sha256");
      
    if(Success && Duration > 0.0f && pPreparedMapPath && pPreparedMapPath[0])  
    {  
        m_pGameContext->QueueMusicPreloadResult(m_Song, m_QueueIndex, true, Duration, m_MapName.c_str(), pPreparedMapPath, pMapSha256, nullptr);  
    }  
    else  
    {  
        const std::string Reason = Success ? std::string("Invalid duration or prepared map path received") : JsonErrorMessage(pJson, "Download status was not success");
        m_pGameContext->QueueMusicPreloadResult(m_Song, m_QueueIndex, false, 0.0f, m_MapName.c_str(), nullptr, nullptr, Reason.c_str());
    }  
      
    json_value_free(pJson);  
}

void CQQRelayJob::Run()
{
	m_pRequest->Wait();
	if(m_pRequest->State() != EHttpState::DONE)
	{
		m_pGameContext->QueueQQRelayResult(m_ClientID, false, "QQ Bot 后端请求失败");
		return;
	}

	json_value *pJson = m_pRequest->ResultJson();
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		m_pGameContext->QueueQQRelayResult(m_ClientID, false, "QQ Bot 返回格式错误");
		return;
	}

	const char *pStatus = JsonStringField(pJson, "status");
	const bool Success = pStatus && str_comp(pStatus, "success") == 0;
	const std::string Message = JsonErrorMessage(pJson, Success ? "喊话发送成功" : "喊话发送失败");
	json_value_free(pJson);
	m_pGameContext->QueueQQRelayResult(m_ClientID, Success, Message.c_str());
}

void CQQPollJob::Run()
{
	m_pRequest->Wait();
	if(m_pRequest->State() != EHttpState::DONE)
	{
		m_pGameContext->QueueQQPollResult(false, {}, "QQ Bot 消息轮询失败");
		return;
	}

	json_value *pJson = m_pRequest->ResultJson();
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		m_pGameContext->QueueQQPollResult(false, {}, "QQ Bot 轮询返回格式错误");
		return;
	}

	std::vector<std::string> vMessages;
	const json_value *pMessages = json_object_get(pJson, "messages");
	if(pMessages->type == json_array)
	{
		for(int i = 0; i < json_array_length(pMessages); ++i)
		{
			const json_value *pEntry = json_array_get(pMessages, i);
			if(pEntry->type != json_object)
				continue;
			const char *pText = JsonStringField(pEntry, "text");
			if(pText && pText[0])
				vMessages.emplace_back(pText);
		}
	}

	json_value_free(pJson);
	m_pGameContext->QueueQQPollResult(true, std::move(vMessages), nullptr);
}

void CMusicHistoryJob::Run()
{
	m_pRequest->Wait();
}

void CMusicStatsJob::Run()
{
	m_pRequest->Wait();
	if(m_pRequest->State() != EHttpState::DONE)
	{
		m_pGameContext->QueueMusicStatsResult(m_ClientID, false, "后端请求失败");
		return;
	}

	json_value *pJson = m_pRequest->ResultJson();
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		m_pGameContext->QueueMusicStatsResult(m_ClientID, false, "后端返回格式错误");
		return;
	}
	const char *pStatus = JsonStringField(pJson, "status");
	const bool Success = !pStatus || str_comp(pStatus, "success") == 0;
	const char *pText = JsonStringField(pJson, "text");
	const std::string Message = pText && pText[0] ? std::string(pText) :
		JsonErrorMessage(pJson, Success ? "暂无内容" : "后端请求失败");
	SMusicEvent Event;
	Event.m_ClientID = m_ClientID;
	Event.m_Success = Success && !Message.empty();
	Event.m_Error = Message;
	Event.m_Undercover = JsonBoolField(pJson, "undercover");
	Event.m_UndercoverActive = JsonBoolField(pJson, "uc_active");
	Event.m_UndercoverClearRoom = JsonBoolField(pJson, "uc_clear_room");
	const char *pAudience = JsonStringField(pJson, "uc_audience");
	Event.m_UndercoverTeamAudience = pAudience && str_comp(pAudience, "team") == 0;
	if(const char *pRoom = JsonStringField(pJson, "uc_room"))
		Event.m_UndercoverRoom = pRoom;
	if(const char *pUcStatus = JsonStringField(pJson, "uc_status"))
		Event.m_UndercoverStatus = pUcStatus;
	if(const char *pPhase = JsonStringField(pJson, "uc_phase"))
		Event.m_UndercoverPhase = pPhase;
	if(const char *pSpeakerId = JsonStringField(pJson, "uc_speaker_id"))
		Event.m_UndercoverSpeakerId = pSpeakerId;
	if(const char *pSpeakerName = JsonStringField(pJson, "uc_speaker_name"))
		Event.m_UndercoverSpeakerName = pSpeakerName;
	JsonIntField(pJson, "uc_deadline", &Event.m_UndercoverDeadline);
	m_pGameContext->QueueMusicStatsEvent(std::move(Event));
	json_value_free(pJson);
}

void CIdentityCodeJob::Run()
{
	m_pRequest->Wait();
	if(m_pRequest->State() != EHttpState::DONE)
	{
		m_pGameContext->QueueIdentityCodeResult(m_ClientID, false, "身份绑定后端请求失败");
		return;
	}

	json_value *pJson = m_pRequest->ResultJson();
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		m_pGameContext->QueueIdentityCodeResult(m_ClientID, false, "身份绑定返回格式错误");
		return;
	}
	const char *pStatus = JsonStringField(pJson, "status");
	const bool Success = pStatus && str_comp(pStatus, "success") == 0;
	const char *pText = JsonStringField(pJson, "text");
	const std::string Message = pText ? pText : JsonErrorMessage(pJson, "验证码生成失败");
	json_value_free(pJson);
	m_pGameContext->QueueIdentityCodeResult(m_ClientID, Success, Message.c_str());
}

void CServerStateJob::Run()
{
	m_pRequest->Wait();
	m_pGameContext->QueueServerStateResult(m_pRequest->State() == EHttpState::DONE);
}

void CTurtleSoupJob::Run()
{
	m_pRequest->Wait();
	if(m_pRequest->State() != EHttpState::DONE)
	{
		m_pGameContext->QueueTurtleSoupResult(m_ClientID, false, "Tee探长: 后端暂时联系不上。");
		return;
	}

	json_value *pJson = m_pRequest->ResultJson();
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		m_pGameContext->QueueTurtleSoupResult(m_ClientID, false, "Tee探长: 线索纸条坏掉了。");
		return;
	}

	const char *pStatus = JsonStringField(pJson, "status");
	const bool Success = pStatus && str_comp(pStatus, "success") == 0;
	const char *pText = JsonStringField(pJson, "text");
	const std::string Message = pText && pText[0] ? std::string(pText) :
		JsonErrorMessage(pJson, Success ? "Tee探长: 暂无回复。" : "Tee探长: 推理失败。");
	json_value_free(pJson);
	m_pGameContext->QueueTurtleSoupResult(m_ClientID, Success, Message.c_str());
}

