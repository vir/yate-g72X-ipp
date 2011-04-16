/**
 * g729codec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * G.729a codec using Intel IPP
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>
#include <ippcore.h>
#include <ipps.h>
#include "g729fpapi.h"

#define G729Encoder_Obj         G729FPEncoder_Obj
#define G729Decoder_Obj         G729FPDecoder_Obj
#define apiG729Encoder_InitBuff apiG729FPEncoder_InitBuff
#define apiG729Decoder_InitBuff apiG729FPDecoder_InitBuff
#define apiG729Encoder_Init     apiG729FPEncoder_Init
#define apiG729Decoder_Init     apiG729FPDecoder_Init
#define apiG729Encode           apiG729FPEncode
#define apiG729Decode           apiG729FPDecode
#define apiG729Encoder_Alloc    apiG729FPEncoder_Alloc
#define apiG729Decoder_Alloc    apiG729FPDecoder_Alloc
#define apiG729Codec_ScratchMemoryAlloc apiG729FPCodec_ScratchMemoryAlloc

#define L_FRAME            80
#define L_FRAME_COMPRESSED 10
typedef short g729_block[L_FRAME];
typedef unsigned char g729_frame[L_FRAME_COMPRESSED];

using namespace TelEngine;

static TranslatorCaps caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

class G729Plugin : public Plugin, public TranslatorFactory
{
public:
    G729Plugin();
    ~G729Plugin();
    virtual void initialize() { }
    virtual bool isBusy() const;
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const;
};

class G729Codec : public DataTranslator
{
public:
    G729Codec(const char* sFormat, const char* dFormat, bool encoding);
    ~G729Codec();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    static int encoderSize;
    static int decoderSize;
    static int coderSizeScratch;
    volatile static int count;

private:
    DataBlock m_data;

    G729FPDecoder_Obj* decoder;
    G729FPEncoder_Obj* encoder;
    Ipp8s *coderScratchMem;
};

int G729Codec::encoderSize;
int G729Codec::decoderSize;
int G729Codec::coderSizeScratch;

volatile int G729Codec::count = 0;

G729Codec::G729Codec(const char* sFormat, const char* dFormat, bool encoding)
    : DataTranslator(sFormat,dFormat), decoder(0), encoder(0)
{
    Debug(DebugAll,"G729Codec::G729Codec(\"%s\",\"%s\",%scoding) [%p]",
        sFormat,dFormat, encoder ? "en" : "de", this);

    coderScratchMem = ippsMalloc_8s(coderSizeScratch);
    if (encoding) {
        encoder = (G729Encoder_Obj*)ippsMalloc_8u(encoderSize);
        apiG729Encoder_InitBuff(encoder, coderScratchMem);
        apiG729Encoder_Init(encoder, G729A_CODEC, G729Encode_VAD_Disabled);
    }
    else {
        decoder = (G729Decoder_Obj*)ippsMalloc_8u(decoderSize);
        apiG729Decoder_InitBuff(decoder, coderScratchMem);
        apiG729Decoder_Init(decoder, G729A_CODEC);
    }
    // gcc way to say ++count atomically
    __sync_add_and_fetch(&count, 1);
}

G729Codec::~G729Codec()
{
    Debug(DebugAll,"G729Codec::~G729Codec() [%p]",this);
    ippsFree(encoder ? (void*)encoder : (void*)decoder);
    ippsFree(coderScratchMem);
    // --count;
    __sync_add_and_fetch(&count, -1);
}

unsigned long G729Codec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!getTransSource())
        return 0;
    if (data.null() && (flags & DataSilent))
        return getTransSource()->Forward(data, tStamp, flags);
    ref();
    m_data += data;
    DataBlock outdata;
    int frames, consumed;
    if (encoder) {
        frames = m_data.length() / sizeof(g729_block);
        consumed = frames * sizeof(g729_block);
        if (frames) {
            outdata.assign(0, frames*sizeof(g729_frame));
            for (int i = 0; i < frames; i++) {
                short* decompressed_buffer = (short*)(((g729_block *)m_data.data())+i);
                unsigned char* compressed_buffer = (unsigned char*)(((g729_frame *)outdata.data())+i);
                int frametype;
                apiG729Encode(encoder, decompressed_buffer, compressed_buffer, G729A_CODEC, &frametype);
            }
        }
        if (!tStamp)
            tStamp = timeStamp() + (consumed / 2);
    }
    else {
        frames = m_data.length() / sizeof(g729_frame);
        consumed = frames * sizeof(g729_frame);
        if (frames) {
            outdata.assign(0, frames*sizeof(g729_block));
            for (int i = 0; i < frames; i++) {
                unsigned char* compressed_buffer = (unsigned char*)(((g729_frame *)m_data.data())+i);
                short* decompressed_buffer = (short*)(((g729_block *)outdata.data())+i);
                int frametype = 3; // active frame
                apiG729Decode(decoder, compressed_buffer, frametype, decompressed_buffer);
            }
        }
        if (!tStamp)
            tStamp = timeStamp() + (frames*sizeof(g729_block) / 2);
    }
    XDebug("G729Codec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
        encoder ? "en" : "de",frames,m_data.length(),consumed,outdata.length());
    unsigned long len = 0;
    if (frames) {
        m_data.cut(-consumed);
        len = getTransSource()->Forward(outdata, tStamp, flags);
    }
    deref();
    return len;
}

G729Plugin::G729Plugin()
    : Plugin("g729codec")
{
    Output("Loaded module G.729a - based on Intel IPP");
    const FormatInfo* f = FormatRepository::addFormat("g729",L_FRAME_COMPRESSED,L_FRAME*1000/8);
    caps[0].src = caps[1].dest = f;
    caps[0].dest = caps[1].src = FormatRepository::getFormat("slin");
    caps[0].cost = caps[1].cost = 5;

    apiG729Decoder_Alloc(G729A_CODEC, &G729Codec::decoderSize);
    apiG729Encoder_Alloc(G729A_CODEC, &G729Codec::encoderSize);
    apiG729Codec_ScratchMemoryAlloc(&G729Codec::coderSizeScratch);
}

G729Plugin::~G729Plugin()
{
    Output("Unloading module G.729a");
}

bool G729Plugin::isBusy() const
{
    return (G729Codec::count != 0);
}

DataTranslator* G729Plugin::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin" && dFormat == "g729")
        return new G729Codec(sFormat,dFormat,true);
    else if (sFormat == "g729" && dFormat == "slin")
        return new G729Codec(sFormat,dFormat,false);
    else return 0;
}

const TranslatorCaps* G729Plugin::getCapabilities() const
{
    return caps;
}

INIT_PLUGIN(G729Plugin);
