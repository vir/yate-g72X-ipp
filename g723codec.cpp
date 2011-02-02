/**
 * g723codec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * G.723.1 codec using Intel IPP
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
#include "g723api.h"

#define L_FRAME               240
#define L_FRAME_COMPRESSED_63 24 // 6.3k
#define L_FRAME_COMPRESSED_53 20 // 5.3k
#define G723_RATE_63 0 // G723_Rate63 in owng723.h
#define G723_RATE_53 1 // G723_Rate53

#define G723_DEFAULT_SEND_RATE G723_RATE_63

#if G723_DEFAULT_SEND_RATE == G723_RATE_63
#define L_FRAME_COMPRESSED L_FRAME_COMPRESSED_63
#else
#define L_FRAME_COMPRESSED L_FRAME_COMPRESSED_53
#endif

typedef short g723_block[L_FRAME];
typedef unsigned char g723_frame[L_FRAME_COMPRESSED];

using namespace TelEngine;

static TranslatorCaps caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

class G723Plugin : public Plugin, public TranslatorFactory
{
public:
    G723Plugin();
    ~G723Plugin();
    virtual void initialize() { }
    virtual bool isBusy() const;
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const;
};

class G723Codec : public DataTranslator
{
public:
    G723Codec(const char* sFormat, const char* dFormat, bool encoding);
    ~G723Codec();
#ifndef YATE_G72X_POST_R2745
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
#else
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
#endif
    static int encoderSize;
    static int decoderSize;
    static int coderSizeScratch;
    volatile static int count;

private:
    int g723_sendrate;
    DataBlock m_data;

    G723Decoder_Obj* decoder;
    G723Encoder_Obj* encoder;
    Ipp8s *coderScratchMem;

    static int G723FrameLength(int frametype);
};

int G723Codec::encoderSize;
int G723Codec::decoderSize;
int G723Codec::coderSizeScratch;

volatile int G723Codec::count = 0;

G723Codec::G723Codec(const char* sFormat, const char* dFormat, bool encoding)
    : DataTranslator(sFormat,dFormat), decoder(0), encoder(0), g723_sendrate(G723_DEFAULT_SEND_RATE)
{
    Debug(DebugAll,"G723Codec::G723Codec(\"%s\",\"%s\",%scoding) [%p]",
        sFormat,dFormat, encoder ? "en" : "de", this);

    coderScratchMem = ippsMalloc_8s(coderSizeScratch);
    if (encoding) {
        encoder = (G723Encoder_Obj*)ippsMalloc_8u(encoderSize);
        apiG723Encoder_InitBuff(encoder, coderScratchMem);
        apiG723Encoder_Init(encoder, G723Encode_DefaultMode);
    }
    else {
        decoder = (G723Decoder_Obj*)ippsMalloc_8u(decoderSize);
        apiG723Decoder_InitBuff(decoder, coderScratchMem);
        apiG723Decoder_Init(decoder, G723Decode_DefaultMode);
    }
    // gcc way to say ++count atomically
    __sync_add_and_fetch(&count, 1);
}

G723Codec::~G723Codec()
{
    Debug(DebugAll,"G723Codec::~G723Codec() [%p]",this);
    ippsFree(encoder ? (void*)encoder : (void*)decoder);
    ippsFree(coderScratchMem);
    // --count;
    __sync_add_and_fetch(&count, -1);
}

int G723Codec::G723FrameLength(int frametype)
{
    switch(frametype) {
        case 0: return 24; /* 6.3kbps */
        case 1: return 20; /* 5.3kbps */
        case 2: return 4;  /* SID */
    }
    return 1; /* XXX untransmitted */
}

#ifndef YATE_G72X_POST_R2745
void G723Codec::Consume(const DataBlock& data, unsigned long tStamp)
#else
unsigned long G723Codec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
#endif
{
    if (!getTransSource())
#ifndef YATE_G72X_POST_R2745
        return;
#else
        return 0;
    if (data.null() && (flags & DataSilent))
        return getTransSource()->Forward(data, tStamp, flags);
#endif
    ref();
    m_data += data;
    DataBlock outdata;
    int frames, consumed;
    if (encoder) {
        frames = m_data.length() / sizeof(g723_block);
        consumed = frames * sizeof(g723_block);
        if (frames) {
            outdata.assign(0, frames*sizeof(g723_frame));
            for (int i = 0; i < frames; i++) {
                short* decompressed_buffer = (short*)(((g723_block *)m_data.data())+i);
                Ipp8s* compressed_buffer = (Ipp8s*)(((g723_frame *)outdata.data())+i);
                int frametype;
                apiG723Encode(encoder, decompressed_buffer, g723_sendrate, compressed_buffer);
            }
        }
        if (!tStamp)
            tStamp = timeStamp() + (consumed / 2);
    }
    else {
        frames = 0;
        int framesize;
        for(consumed = 0; consumed < m_data.length(); consumed += framesize) {
            int frametype = *((unsigned char *)m_data.data() + consumed) & (short)0x0003;
            framesize = G723FrameLength(frametype);
            ++frames;
        }
        outdata.assign(0, frames * sizeof(g723_block));
        frames = 0;
        for(consumed = 0; consumed < m_data.length(); consumed += framesize) {
            int frametype = *((unsigned char *)m_data.data() + consumed) & (short)0x0003;
            framesize = G723FrameLength(frametype);
            const Ipp8s* compressed_buffer = (Ipp8s*)((unsigned char *)m_data.data() + consumed);
            short* decompressed_buffer = (short*)((g723_block*)outdata.data() + frames);
            int badframe = 0; // normal frame
            apiG723Decode(decoder, compressed_buffer, badframe, decompressed_buffer);
            ++frames;
        }
        if (!tStamp)
            tStamp = timeStamp() + (frames*sizeof(g723_block) / 2);
    }
    XDebug("G723Codec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
        encoder ? "en" : "de",frames,m_data.length(),consumed,outdata.length());
#ifdef YATE_G72X_POST_R2745
    unsigned long len = 0;
#endif
    if (frames) {
        m_data.cut(-consumed);
#ifndef YATE_G72X_POST_R2745
        getTransSource()->Forward(outdata,tStamp);
#else
        len = getTransSource()->Forward(outdata, tStamp, flags);
#endif
    }
    deref();
#ifdef YATE_G72X_POST_R2745
    return len;
#endif
}

G723Plugin::G723Plugin()
{
    Output("Loaded module G.723.1 - based on Intel IPP");
    const FormatInfo* f = FormatRepository::addFormat("g723",L_FRAME_COMPRESSED,L_FRAME*1000/8);
    caps[0].src = caps[1].dest = f;
    caps[0].dest = caps[1].src = FormatRepository::getFormat("slin");
    caps[0].cost = caps[1].cost = 5;

    apiG723Decoder_Alloc(&G723Codec::decoderSize);
    apiG723Encoder_Alloc(&G723Codec::encoderSize);
    apiG723Codec_ScratchMemoryAlloc(&G723Codec::coderSizeScratch);
}

G723Plugin::~G723Plugin()
{
    Output("Unloading module G.723.1");
}

bool G723Plugin::isBusy() const
{
    return (G723Codec::count != 0);
}

DataTranslator* G723Plugin::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin" && dFormat == "g723")
        return new G723Codec(sFormat,dFormat,true);
    else if (sFormat == "g723" && dFormat == "slin")
        return new G723Codec(sFormat,dFormat,false);
    else return 0;
}

const TranslatorCaps* G723Plugin::getCapabilities() const
{
    return caps;
}

INIT_PLUGIN(G723Plugin);
