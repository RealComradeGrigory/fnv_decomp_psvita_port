#pragma once
#include "renderer/gxm_renderer.h"
#include "bsa/bsa_reader.h"
#include "dds/dds_loader.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// Bethesda FNT metrics and the matching original font atlas, read from the game BSA.
class OriginalFont {
    struct Glyph { float atlas, uv[8], width, height, left, right, top; };
    Glyph m_glyphs[256]{};
    float m_height = 26;
    GxmTexture m_texture;
    std::vector<BasicVertex> m_vertices;
    std::vector<uint16_t> m_indices;
public:
    bool init(GxmRenderer& renderer, BSAReader* first, BSAReader* second) {
        if (m_texture.isValid()) return true;
        auto extract = [&](const char* file, std::vector<uint8_t>& data) {
            return (second && second->extractFile("textures\\fonts", file, data)) ||
                   (first && first->extractFile("textures\\fonts", file, data));
        };
        std::vector<uint8_t> data;
        if (!extract("glow_monofonto_medium.fnt", data) || data.size() != 296 + sizeof(m_glyphs)) return false;
        memcpy(&m_height, data.data(), 4);
        memcpy(m_glyphs, data.data() + 296, sizeof(m_glyphs));
        if (!std::isfinite(m_height) || m_height <= 0) return false;
        for (const auto& g : m_glyphs) {
            if (!std::isfinite(g.width) || !std::isfinite(g.height) || g.width < 0 || g.width > 512) return false;
            for (float uv : g.uv) if (!std::isfinite(uv) || uv < 0 || uv > 1) return false;
        }
        if (!extract("glow_monofonto_medium_0_lod_a.dds", data)) return false;
        DDSLoader dds;
        return dds.load(data.data(), data.size()) && renderer.createGxmTexture(dds.getTexture(), m_texture);
    }
    bool valid() const { return m_texture.isValid(); }
    void cleanup(GxmRenderer& renderer) { renderer.destroyGxmTexture(m_texture); clear(); }
    void clear() { m_vertices.clear(); m_indices.clear(); }
    float width(const char* text, float height) const {
        float w=0; for (; text && *text; ++text) { const auto& g=m_glyphs[(unsigned char)*text]; w+=(g.width+g.left+g.right)*height/m_height; } return w;
    }
    void text(const char* text, float x, float y, float height, float r, float g, float b, float a=1, float wrap=0) {
        if (!valid() || !text) return;
        const char* begin=text; float start=x, scale=height/m_height;
        for (; *text; ++text) {
            if (*text=='\n') { x=start; y+=height*1.25f; continue; }
            const auto& ch=m_glyphs[(unsigned char)*text];
            float advance=(ch.width+ch.left+ch.right)*scale;
            if (wrap>0 && *text!=' ' && x>start) {
                // Wrap complete words; glyph dimensions remain those in the FNT.
                if (text[0] && (x==start || (text>begin && text[-1]==' '))) {
                    float word=0; for (const char* p=text; *p && *p!=' ' && *p!='\n'; ++p) { const auto& c=m_glyphs[(unsigned char)*p]; word+=(c.width+c.left+c.right)*scale; }
                    if (x+word>start+wrap) { x=start; y+=height*1.25f; }
                }
            }
            if (ch.width>0 && ch.height>0 && m_vertices.size()+4<=65536) {
                uint16_t base=m_vertices.size(); float xx=x+ch.left*scale, yy=y+(m_height-ch.top)*scale;
                for (int j=0;j<4;++j) {
                    BasicVertex v{}; v.position[0]=xx+(j&1)*ch.width*scale; v.position[1]=yy+(j>>1)*ch.height*scale;
                    v.normal[2]=1;v.color[0]=r;v.color[1]=g;v.color[2]=b;v.color[3]=a;v.uv[0]=ch.uv[j*2];v.uv[1]=ch.uv[j*2+1]; m_vertices.push_back(v);
                }
                for (uint16_t k : {0,1,2,2,1,3}) m_indices.push_back(base+k);
            }
            x+=advance;
        }
    }
    void flush(GxmRenderer& renderer) {
        if (!m_indices.empty()) renderer.drawTextured2D(m_vertices.data(),m_vertices.size(),m_indices.data(),m_indices.size(),&m_texture);
        clear();
    }
};
