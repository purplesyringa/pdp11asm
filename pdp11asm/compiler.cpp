// PDP11 Assembler (c) 15-01-2015 vinxru

#include "compiler.h"
#include <string.h>
#include <stdio.h>
#include <fstream> //! ??????
#include "fstools.h"
#include "c_parser.h"
#include "c_compiler_pdp11.h"

static unsigned char cp1251_to_koi8r_tbl[256] = {
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
  32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
  64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
  96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
  128,129,130,131,132,133,134,135,136,137,060,139,140,141,142,143,144,145,146,147,148,169,150,151,152,153,154,062,176,157,183,159,
  160,246,247,074,164,231,166,167,179,169,180,060,172,173,174,183,156,177,073,105,199,181,182,158,163,191,164,062,106,189,190,167,
  225,226,247,231,228,229,246,250,233,234,235,236,237,238,239,240,242,243,244,245,230,232,227,254,251,253,154,249,248,252,224,241,
  193,194,215,199,196,197,214,218,201,202,203,204,205,206,207,208,210,211,212,213,198,200,195,222,219,221,223,217,216,220,192,209
};

//-----------------------------------------------------------------------------

static void cp1251_to_koi8r(char* str) {
  for(;*str; str++)
    *str = (char)cp1251_to_koi8r_tbl[(unsigned char)*str];
}

//-----------------------------------------------------------------------------

Compiler::Compiler() {
  convert1251toKOI8R = false;
  processor = P_PDP11;
  needCreateOutputFile = true;
  lstWriter.out = &out;
  lstWriter.p = &p;
  p.cfg_eol = true;
  p.cfg_caseSel = false;
  static const char* asmRem[] = { ";", "//", 0 };
  p.cfg_remark = asmRem;
  static const char* asmOp[] = { "//", 0 };
  p.cfg_operators = asmOp;
}

//-----------------------------------------------------------------------------

bool Compiler::ifConst4(Parser::num_t& out, bool numIsLabel) {
  if(regInParser()) return false;
  if(numIsLabel && p.ifToken(ttInteger)) {
    makeLocalLabelName();
    goto itsLabel;
  }
  if(p.ifToken(ttWord)) {
itsLabel:
    std::map<std::string, Parser::num_t>::iterator l = labels.find(p.loadedText);
    if(l != labels.end()) {
      out = l->second;
      return true;
    }
    if(step2) p.syntaxError(p.loadedText);
    out = 16384;
    return true;
  }
  if(p.ifToken(ttString1)) {
    out = (unsigned char)p.loadedText[0];
    if(convert1251toKOI8R) out = cp1251_to_koi8r_tbl[out];
    return true;
  }
  if(p.ifToken(ttInteger)) {
    out = p.loadedNum;
    return true;
  }
  Parser::Label l(p);
  if(p.ifToken("-")) {
    if(p.ifToken(ttInteger)) {
      out = 0-p.loadedNum;
      return true;
    }
    p.jump(l);
  }
  Parser::Label pl(p);
  if(p.ifToken("(")) {
    if(regInParser()) { p.jump(pl); return false; }
    out = readConst3(numIsLabel);
    p.needToken(")");
    return true;
  }
  if(p.ifToken(".") || p.ifToken("$")) {
    out = this->out.writePtr;
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------

bool Compiler::ifConst3(Parser::num_t& a, bool numIsLabel) {
  if(!ifConst4(a, numIsLabel)) return false;
  static const char* ops[] = { "+", "-", "*", "/", 0 };
  int o;
  while(p.ifToken(ops, o)) {
    Parser::num_t b;
    if(!ifConst4(b, numIsLabel)) p.syntaxError();
    switch(o) {
      case 0: a += b; break;
      case 1: a -= b; break;
      case 2: a *= b; break;
      case 3: a /= b; break;
    }
  }
  return true;
}

//-----------------------------------------------------------------------------

Parser::num_t Compiler::readConst3(bool numIsLabel) {
  Parser::num_t i;
  if(!ifConst3(i, numIsLabel)) p.syntaxError();
  return i;
}

//-----------------------------------------------------------------------------

void Compiler::compileOrg() {
  Parser::num_t o = readConst3();
  if(o < 0 || o > sizeof(out.writeBuf)) p.syntaxError();
  out.writePosChanged = true;
  out.writePtr = (size_t)o;
  return;
}

//-----------------------------------------------------------------------------

void Compiler::compileByte() {
  for(;;) {
    Parser::num_t c;
    if(p.ifToken(ttString1) || p.ifToken(ttString2)) {
      if(convert1251toKOI8R) cp1251_to_koi8r(p.loadedText);
      out.write(p.loadedText, strlen(p.loadedText));
    } else
    if(ifConst3(c)) {
      if(p.ifToken("dup")) {
        p.needToken("(");
        Parser::num_t d = readConst3();
        if(d>std::numeric_limits<unsigned char>::max()) p.syntaxError();
        p.needToken(")");
        for(;c>0; c--) out.write8((unsigned char)d);
      } else {
        if(c>std::numeric_limits<unsigned char>::max()) p.syntaxError();
        out.write8((unsigned char)c);
      }
    } else {
      p.syntaxError();
    }
    if(!p.ifToken(",")) break;
  }
}

//-----------------------------------------------------------------------------

void Compiler::compileWord() {
  for(;;) {
    Parser::num_t c = readConst3();
    if(p.ifToken("dup")) {
      p.needToken("(");
      Parser::num_t d = readConst3();
      if(d>std::numeric_limits<unsigned short>::max()) p.syntaxError();
      p.needToken(")");
      for(;c>0; c--) out.write16((short)d);
    } else {
      if(c>std::numeric_limits<unsigned short>::max()) p.syntaxError();
      out.write16((short)c);
    }
    if(!p.ifToken(",")) break;
  }
}

//-----------------------------------------------------------------------------

void Compiler::makeLocalLabelName() {
  // p.loadedNum is unsigned
  if(p.loadedNum > std::numeric_limits<int>::max()) p.syntaxError();
  snprintf(p.loadedText, sizeof(p.loadedText), "%s@%i", lastLabel, (int)p.loadedNum); //! overflow
}

//-----------------------------------------------------------------------------

bool Compiler::compileLine2() {
//retry:
  // ????? ? ????????? ???????????? ?? ??????? ?????
  Parser::Label l(p);
  p.nextToken();
  // ?????? ????? ?????? ???? ?????? ??? ???????.
//  bool label = (p.token==ttOperator && p.tokenText[0]==':' && p.tokenText[1]==0);
//  bool datadecl = (p.token==ttWord && (0==strcmp(p.tokenText, "DB") || 0==strcmp(p.tokenText, "DW") || 0==strcmp(p.tokenText, "DS")));
  bool equ   = (p.token==ttWord && 0==strcmp(p.tokenText, "EQU")) || (p.token==ttOperator && 0==strcmp(p.tokenText, "="));
  p.jump(l);

  // ??? ?????????
  if(equ) {
    p.needToken(ttWord);
    Parser::TokenText name;
    strcpy(name, p.loadedText);
    if(!p.ifToken("=")) p.needToken("equ");
    Parser::num_t a = readConst3();
    if(!step2) labels[name] = a;
    return true;
  }

  // ????????? ??????
  if(p.ifToken("org")) {
    compileOrg();
    return true;
  }

    // ??????? MACRO11
    if(p.ifToken(".")) {
        if(p.ifToken("include")) {
            p.needToken(ttString2);
            std::string fileName1 = p.loadedText;
            std::string buf;
            loadStringFromFile(buf,  p.loadedText);
            p.enterMacro(0, &buf, -1, false /*, true*/);
            p.fileName = fileName1;
            return true;
        };
    // ????? ??????????
    if(p.ifToken("i8080")) {
      lstWriter.hexMode = true;
      processor = P_8080;
      p.cfg_decimalnumbers = true;
      return true;
    }
    if(p.ifToken("PDP11")) {
      lstWriter.hexMode = false;
      processor = P_PDP11;
      return true;
    }
    // ???????? ?????
    if(p.ifToken("db") || p.ifToken("byte")) {
      compileByte();
      return true;
    }
    if(p.ifToken("dw") || p.ifToken("word")) {
      compileWord();
      return true;
    }
    if(p.ifToken("end")) {
      return true;
    }
    if(p.ifToken("link")) {
      compileOrg();
      return true;
    }
    if(p.ifToken("ds") || p.ifToken("blkb")) {
      p.needToken(ttInteger);
      for(;p.tokenNum>0; p.tokenNum--) out.write8(0);
      return true;
    }
    if(p.ifToken("blkw")) {
      p.needToken(ttInteger);
      for(;p.tokenNum>0; p.tokenNum--) out.write16(0);
      return true;
    }
    p.altstring = '/';
    if(p.ifToken("ascii")) {
      p.altstring = 0;
      p.needToken(ttString2);
      if(convert1251toKOI8R) cp1251_to_koi8r(p.loadedText);
      out.write(p.loadedText, strlen(p.loadedText));
      return true;
    }
    p.altstring = 0;
    p.syntaxError();
  }

  // ???????? ????????? ?????
  bool make_binary_file = p.ifToken("make_binary_file");
  if(make_binary_file || p.ifToken("make_bk0010_rom")) {
    needCreateOutputFile = false;
    p.needToken(ttString2);
    Parser::TokenText fileName;
    strcpy(fileName, p.loadedText);
    size_t start = 0, stop = out.writePtr;
    if(p.ifToken(",")) {
      start = ullong2size_t(readConst3());
      if(p.ifToken(",")) stop = ullong2size_t(readConst3());
    }
    // ???????? ?????? ?? ?????? ???????
    if(step2) {
      if(stop<=start || stop>sizeof(out.writeBuf)) p.syntaxError();
      size_t length = stop - start;


      std::string o;
      o.append((const char*)&start, 2);
      o.append((const char*)&length, 2);
      o.append(out.writeBuf+start, length);
      saveStringToFile(fileName, o.c_str(), o.size());
      /*
      std::ofstream f;
      f.open(fileName, std::ofstream::binary|std::ofstream::out);
      if(!f.is_open()) p.syntaxError("Can't create file");
      if(!make_binary_file) {
        f.write((const char*)&start, 2);
        f.write((const char*)&length, 2);
      }
      f.write(out.writeBuf+start, length);
      f.close();
      */
      lstWriter.writeFile(fileName);
    }
    return true;
  }

  if(p.ifToken("convert1251toKOI8R")) {
    convert1251toKOI8R = !p.ifToken("OFF");
    return true;
  }

  if(p.ifToken("decimalnumbers")) {
    p.cfg_decimalnumbers = !p.ifToken("OFF");
    return true;
  }

  if(p.ifToken("insert_file")) {
    p.needToken(ttString2);
    Parser::TokenText fileName;
    strcpy(fileName, p.loadedText);
    size_t start=0, size=0;
    if(p.ifToken(",")) {
      start = ullong2size_t(readConst3());
      if(p.ifToken(",")) size = ullong2size_t(readConst3());
    }
    std::ifstream f; //! ????????
    if(size==0 || step2) {
      f.open(fileName, std::ifstream::binary|std::ifstream::in);
      if(!f.is_open()) p.syntaxError("Can't open file");
      if(size==0) size = (size_t)f.rdbuf()->pubseekoff(0, std::ifstream::end);  //! ??? ????? ???? ????????????
    }
    if(size<0 || out.writePtr+size>=65536) p.syntaxError();
    if(step2) {
      f.rdbuf()->pubseekoff(start, std::ifstream::beg);
      f.rdbuf()->sgetn(out.writePtr+out.writeBuf, size);
      out.write(out.writePtr+out.writeBuf, size);
    }
//    out.writePtr += (int)size; // ???????????? ????????? ????
    return true;
  }

  // ????????? ???
  if(p.ifToken("align")) {
    p.needToken(ttInteger);
    if(p.loadedNum < 1 || p.loadedNum >= std::numeric_limits<size_t>::max()) p.syntaxError();
    size_t a = size_t(p.loadedNum);
    out.writePtr = (out.writePtr + a - 1) / a * a;
    return true;
  }
  // ???????? ?????
  if(p.ifToken("db")) {
    compileByte();
    return true;
  }
  // ???????? ?????
  if(p.ifToken("dw")) {
    compileWord();
    return true;
  }
  if(p.ifToken("end")) return true;
  if(p.ifToken("ds")) {
    p.needToken(ttInteger);
    for(;p.tokenNum>0; p.tokenNum--) out.write8(0);
    return true;
  }
  switch(processor) {
    case P_PDP11: if(compileLine_pdp11()) return true; break;
    case P_8080: if(compileLine_8080()) return true; break;
  }
  if(compileLine_bitmap()) return true;
  return false;
}

void Compiler::compileLine() {
  for(;;) {
  if(compileLine2()) return;

  // ??? ?????
  if(p.ifToken(ttInteger)) {
    makeLocalLabelName();
    labels[p.loadedText] = out.writePtr;
  } else {
    p.needToken(ttWord);
    labels[p.loadedText] = out.writePtr;
    strcpy(lastLabel, p.loadedText);
  }
  // ?? ???????????
  p.ifToken(":");

  // ????? ????? ????? ???? ???????
  if(p.token == ttEol) return;
  if(p.token == ttEof) return;
  }
}

//-----------------------------------------------------------------------------
// Расстановка адресов

void Compiler::processLabels()
{
    for(unsigned i=0; i<fixups.size(); i++)
    {
        Fixup& f = fixups[i];
        std::map<std::string, Parser::num_t>::iterator j = labels.find(f.name);
        if(j == labels.end()) throw std::runtime_error("Метка "+f.name+" не найдена");
        *(uint16_t*)(out.writeBuf + f.addr) = j->second;
    }
}

//-----------------------------------------------------------------------------

void Compiler::compileFile(const syschar_t* fileName) {
  // ???????? ?????
  loadStringFromFile(p.source, fileName);

  // ????????? ???? ??? INCLUDE-??????
  chdirToFile(fileName);

  C::Tree world;
  C::CompilerPdp11 cc(*this, world);
  C::Parser cp(p, world);

  // ??? ???????
  out.clear();
  for(int s=0; s<2; s++) {
    step2 = s==1;
    p.init(p.source.c_str());
    out.init();
    strcpy(lastLabel, "undefined");
    while(!p.ifToken(ttEof)) {
      if(p.ifToken(ttEol)) continue;

      // ????????? ??????
      if(p.ifToken("{")) {
          cp.parse(s);
          cc.start(s);
      }
      else
      {
        if(step2) lstWriter.beforeCompileLine();
        compileLine();
        if(step2) lstWriter.afterCompileLine3();
      }
      if(p.ifToken(ttEof)) break;
      if(s==0) processLabels();
      //! ?????? ? ????? ?????? ????? ???? ????? ??????  p.needToken(ttEol);
    }
  }

  if(needCreateOutputFile && out.min<out.max) {
    sysstring_t fileName2 = replaceExtension(fileName, "bin");
    if(fileName != fileName2) {
      saveStringToFile(fileName2.c_str(), out.writeBuf+out.min, out.max-out.min);
      /*
      std::ofstream f;
      f.open(fileName2.c_str(), std::ofstream::binary|std::ofstream::out);
      if(!f.is_open()) p.syntaxError("Can't create file");
      f.write(out.writeBuf+out.min, out.max-out.min);
      f.close();
      lstWriter.writeFile(fileName);
      */
    }
  }
}

//-----------------------------------------------------------------------------
// Дизассемблер

void Compiler::disassembly(unsigned s, unsigned e)
{
    size_t r = 0;
    while(s<e)
    {
        while(r<lstWriter.remarks.size() && lstWriter.remarks[r].addr <= s)
        {
            LstWriter::Remark& re = lstWriter.remarks[r];
            if(re.addr == s)
            {
                lstWriter.beforeCompileLine();
                lstWriter.afterCompileLine2();
                if(re.type==0) lstWriter.appendBuffer("//");
                lstWriter.appendBuffer(re.text.c_str());
                if(re.type==1) lstWriter.appendBuffer(":");
                lstWriter.appendBuffer("\r\n");
            }
            r++;
        }

        lstWriter.beforeCompileLine();
        char buf[disassemblyPdp11OutSize];
        s += disassemblyPdp11(buf, (uint16_t*)(out.writeBuf + s), e-s, s);
        out.writePtr = s;
        lstWriter.afterCompileLine2();
        lstWriter.appendBuffer("    ");
        lstWriter.appendBuffer(buf);
        lstWriter.appendBuffer("\r\n");
    }
}
