
struct NativeFun;
struct SymbolTable;

struct LineInfo : Serializable
{
    int line;
    int fileidx;
    int bytecodestart;

    LineInfo()                    : line(-1), fileidx(-1), bytecodestart(-1) {}
    LineInfo(int l, int i, int b) : line(l),  fileidx(i),  bytecodestart(b)  {}

    void Serialize(Serializer &ser)
    {
        ser(line);
        ser(fileidx);
        ser(bytecodestart);
    }
};

struct SubFunction;

struct Ident : Name
{
    int line;
    size_t scope;
    
    Ident *prev;

    SubFunction *sf;
    
    bool single_assignment;
    bool constant;
    bool static_constant;

    Ident()                                                  :                    line(-1), scope( -1), prev(NULL), sf(NULL), single_assignment(true), constant(false), static_constant(false) {}
    Ident(const string &_name, int _l, int _idx, size_t _sc) : Name(_name, _idx), line(_l), scope(_sc), prev(NULL), sf(NULL), single_assignment(true), constant(false), static_constant(false) {}

    void Serialize(Serializer &ser)
    {
        Name::Serialize(ser);
        ser(line);
        ser(static_constant);
    }
    
    void Assign(Lex &lex)
    {
        single_assignment = false;
        if (constant)
            lex.Error("variable " + name + " is constant");
    }
};

struct FieldOffset
{
    short structidx, offset;

    FieldOffset()                : structidx(-1),  offset(-1) {}
    FieldOffset(int _si, int _o) : structidx(_si), offset(_o) {}
};

struct SharedField : Name
{
    vector<FieldOffset> offsets;

    int numunique;
    FieldOffset fo1, foN;    // in the case of 2 unique offsets where fo1 has only 1 occurence, and foN all the others
    int offsettable;         // in the case of N offsets (bytecode index)

    SharedField()                              :                    numunique(0), offsettable(-1) {}
    SharedField(const string &_name, int _idx) : Name(_name, _idx), numunique(0), offsettable(-1) {}

    void NewFieldUse(const FieldOffset &nfo)
    {
        for (auto &fo : offsets) if (fo.offset == nfo.offset) goto found;
        numunique++;
        found:
        offsets.push_back(nfo);
    }
};

struct UniqueField
{
    Type type;

    UniqueField(Type _t) : type(_t) {}
};

struct Struct : Name
{
    vector<pair<UniqueField, SharedField *>> fields; // SharedField * are shared between all Structs that have them

    Struct *superclass;
    int superclassidx;
    
    bool readonly;

    Struct()                                                 : superclass(NULL), superclassidx(-1), readonly(false) {}
    Struct(const string &_name, int _idx) : Name(_name, _idx), superclass(NULL), superclassidx(-1), readonly(false) {}

    void Serialize(Serializer &ser)
    {
        Name::Serialize(ser);
        ser(superclassidx);
        ser(readonly);
    }

    bool Has(SharedField *fld)
    {
        for (auto &f : fields) if (f.second == fld) return true;
        return false;
    }
};

struct Function;

struct SubFunction
{
    Arg *args;

    Node *body;

    SubFunction *next;
    Function *parent;

    int subbytecodestart;

    SubFunction(Function *_p) : parent(_p), args(NULL), body(NULL), next(NULL), subbytecodestart(0) {}

    ~SubFunction()
    {
        if (args) delete[] args;
        if (next) delete next;
    } 
};

struct Function : Name
{
    int nargs;
    int bytecodestart;

    SubFunction *subf;  // functions with the same name and args, but different types (dynamic dispatch) 
    Function *sibf;     // functions with the same name but different args (overloaded)

    int scopelevel;
    int retvals;

    int ncalls;         // used by codegen to cull unused functions

    Function()                                                   :                    nargs(-1),     bytecodestart(-1), subf(NULL), sibf(NULL), scopelevel(-1),  retvals(0), ncalls(0) {}
    Function(const string &_name, int _idx, int _nargs, int _sl) : Name(_name, _idx), nargs(_nargs), bytecodestart(0),  subf(NULL), sibf(NULL), scopelevel(_sl), retvals(0), ncalls(0) {}
    ~Function() { if (subf) delete subf; }

    void Serialize(Serializer &ser)
    {
        Name::Serialize(ser);
        ser(nargs);
        ser(bytecodestart);
        ser(retvals);
    }
};

struct SymbolTable
{
    map<string, Ident *> idents;
    vector<Ident *> identtable;
    vector<Ident *> identstack;

    map<string, Struct *> structs;
    vector<Struct *> structtable;

    map<string, SharedField *> fields;
    vector<SharedField *> fieldtable;

    map<string, Function *> functions;
    vector<Function *> functiontable;

    vector<string> filenames;
    
    vector<size_t> scopelevels;

    vector<pair<Type, Ident *>> withstack;
    vector<size_t> withstacklevels;

    SymbolTable() {}

    ~SymbolTable()
    {
        for (auto id : identtable)    delete id;
        for (auto st : structtable)   delete st;
        for (auto f  : functiontable) delete f;
        for (auto f  : fieldtable)    delete f;
    }
    
    Ident *LookupLexDefOrDynScope(const string &name, int line, Lex &lex, bool dynscope, SubFunction *sf)
    {
        auto it = idents.find(name);
        if (dynscope && it != idents.end()) return it->second;

        Ident *ident = NULL;
        if (LookupWithStruct(name, lex, ident)) lex.Error("cannot define variable with same name as field in this scope: " + name);
        ident = new Ident(name, line, identtable.size(), scopelevels.back());
        ident->sf = sf;

        if (it == idents.end() || dynscope) idents[name] = ident;
        else
        {
            if (scopelevels.back() == it->second->scope) lex.Error("identifier redefinition: " + ident->name);
            ident->prev = it->second;
            it->second = ident;
        }
        identstack.push_back(ident);
        identtable.push_back(ident);
        return ident;
    }

    Ident *LookupLexMaybe(const string &name)
    {
        auto it = idents.find(name);
        return it == idents.end() ? NULL : it->second;  
    }

    Ident *LookupLexUse(const string &name, Lex &lex)
    {
        auto id = LookupLexMaybe(name);
        if (!id)
            lex.Error("unknown identifier: " + name);
        return id;  
    }

    Ident *LookupIdentInFun(const string &idname, const string &fname) // slow, but infrequently used
    {
        Ident *found = NULL;
        for (auto id : identtable)  
            if (id->name == idname && id->sf && id->sf->parent->name == fname)
            {
                if (found) return NULL;
                found = id;
            }

        return found;
    }

    void AddWithStruct(Type &t, Ident *id, Lex &lex)
    {
        for (auto &wp : withstack) if (wp.first.idx == t.idx) lex.Error("type used twice in the same scope with ::");
        // FIXME: should also check if variables have already been defined in this scope that clash with the struct, or do so in LookupLexUse
        assert(t.idx >= 0);
        withstack.push_back(make_pair(t, id));
    }

    SharedField *LookupWithStruct(const string &name, Lex &lex, Ident *&id)
    {
        auto fld = FieldUse(name);
        if (!fld) return NULL;

        assert(!id);
        for (auto &wp : withstack)
        {
            if (structtable[wp.first.idx]->Has(fld))
            {
                if (id) lex.Error("access to ambiguous field: " + fld->name);
                id = wp.second;
            }
        }

        return id ? fld : NULL;
    }
    
    void ScopeStart()
    {
        scopelevels.push_back(identstack.size());
        withstacklevels.push_back(withstack.size());
    }

    void ScopeCleanup(Lex &lex)
    {
        while (identstack.size() > scopelevels.back())
        {
            auto ident = identstack.back();
            auto it = idents.find(ident->name);
            if (it != idents.end())   // can already have been removed by private var cleanup
            {
                if (ident->prev) it->second = ident->prev;
                else idents.erase(it);
            }
            
            identstack.pop_back();
        }
        scopelevels.pop_back();

        while (withstack.size() > withstacklevels.back()) withstack.pop_back();
        withstacklevels.pop_back();
    }

    void UnregisterStruct(Struct *st)
    {
        auto it = structs.find(st->name);
        assert(it != structs.end());
        structs.erase(it);
    }

    void UnregisterFun(Function *f)
    {
        auto it = functions.find(f->name);
        if (it != functions.end())  // it can already have been remove by another variation
            functions.erase(it);
    }
    
    void EndOfInclude()
    {
        auto it = idents.begin();
        while (it != idents.end())
        {
            if (it->second->isprivate)
            {
                assert(!it->second->prev);
                idents.erase(it++);
            }
            else
                it++;
        }
    }

    Struct &StructDecl(const string &name, Lex &lex)
    {
        Struct *st = structs[name];
        if (st) lex.Error("double declaration of type: " + name);
        st = new Struct(name, structtable.size());
        structs[name] = st;
        structtable.push_back(st);
        return *st;
    }

    Struct &StructUse(const string &name, Lex &lex)
    {
        Struct *st = structs[name];
        if (!st) lex.Error("unknown type: " + name);
        return *st;
    }

    int StructIdx(const string &name, size_t &nargs) // FIXME: this is inefficient, used by parse_data()
    {
        for (auto s : structtable) if (s->name == name) 
        {
            nargs = s->fields.size();
            return s->idx;
        }
        return -1;
    }

    SharedField &FieldDecl(const string &name, int idx, Struct *st, Lex &lex)
    {
        SharedField *fld = fields[name];
        if (!fld)
        {
            fld = new SharedField(name, fieldtable.size());
            fields[name] = fld;
            fieldtable.push_back(fld);
        }
        fld->NewFieldUse(FieldOffset(st->idx, idx));
        return *fld;
    }

    SharedField *FieldUse(const string &name)
    {
        auto it = fields.find(name);
        return it != fields.end() ? it->second : NULL;
    }

    Function &FunctionDecl(const string &name, int nargs, Lex &lex)
    {
        auto fit = functions.find(name);

        if (fit != functions.end())
        {
            if (fit->second->scopelevel != int(scopelevels.size()))
                lex.Error("cannot define a variation of function " + name + " at a different scope level");

            for (auto f = fit->second; f; f = f->sibf)
                if (f->nargs == nargs)
                    return *f;
        }

        auto f = new Function(name, functiontable.size(), nargs, scopelevels.size());
        functiontable.push_back(f);

        if (fit != functions.end())
        {
            f->sibf = fit->second->sibf;
            fit->second->sibf = f;
        }
        else
        {
            functions[name] = f;
        }

        return *f;
    }

    Function *FindFunction(const string &name)
    {
        auto it = functions.find(name);
        return it != functions.end() ? it->second : NULL;
    }

    bool ReadOnlyIdent(uint v) { assert(v < identtable.size());    return identtable[v]->constant;  }
    bool ReadOnlyType (uint v) { assert(v < structtable.size());   return structtable[v]->readonly; }
    
    string &ReverseLookupIdent   (uint v) { assert(v < identtable.size());    return identtable[v]->name;    }
    string &ReverseLookupType    (uint v) { assert(v < structtable.size());   return structtable[v]->name;   }
    string &ReverseLookupFunction(uint v) { assert(v < functiontable.size()); return functiontable[v]->name; }

    void Serialize(Serializer &ser, vector<int> &code, vector<LineInfo> &linenumbers)
    {
        auto curvers = __DATE__; // __TIME__;
        string vers = curvers;
        ser(vers);
        if (ser.rbuf && vers != curvers) throw string("cannot load bytecode from a different version of the compiler");

        ser(identtable);
        ser(functiontable);
        ser(structtable);
        ser(fieldtable);

        ser(code);
        ser(filenames);
        ser(linenumbers);
    }
};
