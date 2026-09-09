#pragma once

#include <string>
#include <vector>

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <comdef.h>
#include <comip.h>

#include "com.hpp"
#include "pipe_client.h"
#include "voice_registry.hpp"

namespace Infovox {
namespace sapi {

class __declspec(uuid("{ae1b20fd-d7f4-4113-bc9b-58407344a593}")) ISpTTSEngineImpl :
    public ISpTTSEngine, public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags, REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx,
                     const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;
    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId,
                               const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId,
                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));

    // Speaks one fragment's worth of prepared text and pumps the audio into the
    // site, returning false once SAPI asks for the utterance to be abandoned.
    bool speakFragment(const SPVTEXTFRAG* frag, ISpTTSEngineSite* site,
                       bool spellOut, ULONGLONG* streamSamples);

    ISpObjectTokenPtr token_;
    std::string voiceId_ = "AM01";   // 'AM01'; empty means the custom voice
    bool custom_ = false;
    PipeClient pipe_;
};

}  // namespace sapi
}  // namespace Infovox
