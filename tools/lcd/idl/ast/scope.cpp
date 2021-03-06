#include "lcd_ast.h"
#include <stdio.h>

GlobalScope* GlobalScope::instance_ = 0;

GlobalScope::GlobalScope()
{
   // init builtin int types
  this->outer_scope_ = 0x0; // global scope 

  // move this code to wherever we create the root scope.
  // instert for each builtin in type, add size to type if not done alreayd

  this->type_definitions_.insert( std::pair<std::string, Type*>("bool", new BoolType()));
  this->type_definitions_.insert( std::pair<std::string, Type*>("double", new DoubleType()));
  this->type_definitions_.insert( std::pair<std::string, Type*>("float", new FloatType()));
  this->type_definitions_.insert( std::pair<std::string,Type*>("void", new VoidType()));
  this->type_definitions_.insert( std::pair<std::string,Type*>("char"
					       , new IntegerType(pt_char_t, false, sizeof(char))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("unsigned char"
					       , new IntegerType(pt_char_t, true, sizeof(char))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("short"
					      , new IntegerType(pt_short_t, false, sizeof(short))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("unsigned short"
					      , new IntegerType(pt_short_t, true, sizeof(short))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("int"
					      , new IntegerType(pt_int_t, false, sizeof(int))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("unsigned int"
					      , new IntegerType(pt_int_t, true, sizeof(int))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("long"
					      , new IntegerType(pt_long_t, false, sizeof(long))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("unsigned long"
					      , new IntegerType(pt_long_t, true, sizeof(long))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("long long"
					      , new IntegerType(pt_longlong_t, false, sizeof(long long))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("unsigned long long"
					      , new IntegerType(pt_longlong_t, true, sizeof(long long))));
  this->type_definitions_.insert( std::pair<std::string,Type*>("capability"
					       , new IntegerType(pt_capability_t, false, sizeof(int))));
  // dptpr_t
  std::vector<ProjectionField*> fields;
  int err;
  fields.push_back(new ProjectionField(this->lookup("unsigned long", &err), "dptr", 0)); // unsigned long dptr;
  this->type_definitions_.insert( std::pair<std::string, Type*>("dptr_t"
								, new ProjectionType("dptr_t", "dptr_t", fields)));
  
  // cptr_t
  std::vector<ProjectionField*> fields2;
  ProjectionField *cptr = new ProjectionField(this->lookup("unsigned long", &err), "cptr", 0); // unsigned long cptr;
  if(err == 0) {
    printf("COULD NOT FIND UNSIGNED LONG\N");
  }
  cptr->set_in(true);
  cptr->set_out(true);
  fields2.push_back(cptr);
  
  this->type_definitions_.insert( std::pair<std::string, Type*>("cptr_t"
								, new ProjectionType("cptr_t", "cptr_t", fields2)));


  // dstore no fields
  std::vector<ProjectionField*> fields3;
  this->type_definitions_.insert(std::pair<std::string, Type*>("dstore"
							       , new ProjectionType("dstore", "dstore", fields3)));

  std::vector<ProjectionField*> fields4;
  fields4.push_back(new ProjectionField(this->lookup("void", &err), "hidden_args", 1));
  //fields4.push_back( char trampoline[0]); 
  this->type_definitions_.insert(std::pair<std::string, Type*>("lcd_trampoline_handle"
							       , new ProjectionType("lcd_trampoline_handle", "lcd_trampoline_handle", fields4)));

  // cspace
  std::vector<ProjectionField*> cspace_fields;
  this->type_definitions_.insert(std::pair<std::string, Type*>("cspace"
							       , new ProjectionType("cspace", "cspace", cspace_fields)));

  // cptr_cache
  std::vector<ProjectionField*> cptr_cache_fields;
  this->type_definitions_.insert(std::pair<std::string, Type*>("cptr_cache"
							       , new ProjectionType("cptr_cache", "cptr_cache", cptr_cache_fields)));

  // glue_cspace
  std::vector<ProjectionField*> glue_cspace_fields;
  glue_cspace_fields.push_back(new ProjectionField(this->lookup("cspace", &err), "cspace", 1)); // cspace
  glue_cspace_fields.push_back(new ProjectionField(this->lookup("cptr_cache", &err), "cptr_cache", 1)); // cptr_cache

  this->type_definitions_.insert(std::pair<std::string, Type*>("glue_cspace"
							       , new ProjectionType("glue_cspace", "glue_cspace", glue_cspace_fields)));

  // lcd_sync_channel_group
  std::vector<ProjectionField*> lcd_sync_channel_group_fields;
  this->type_definitions_.insert(std::pair<std::string, Type*>("lcd_sync_channel_group"
							       , new ProjectionType("lcd_sync_channel_group", "lcd_sync_channel_group", lcd_sync_channel_group_fields)));
}

GlobalScope* GlobalScope::instance()
{
  if(!GlobalScope::instance_)
    GlobalScope::instance_ = new GlobalScope();
  return instance_;
}

void GlobalScope::set_outer_scope(LexicalScope *ls)
{
  printf("error:  attempt to set outer scope of global scope\n");
  return;
}

/* -------------------------------------------------------------- */


LexicalScope::LexicalScope()
{
  this->outer_scope_ = 0x0;
}

LexicalScope::LexicalScope(LexicalScope *outer_scope)
{
  this->outer_scope_ = outer_scope;
}

bool LexicalScope::insert_identifier(const char* id)
{
  std::string temp(id);
  if(this->contains_identifier(id)) {
    return false;
  }
  this->identifiers_.push_back(temp);
  return true;
}

bool LexicalScope::contains_identifier(const char* id)
{
  std::string temp(id);

  for(std::vector<std::string>::iterator it = this->identifiers_.begin(); it != this->identifiers_.end(); it ++) {
    std::string s = *it;
    if (s.compare(temp) == 0) {
      return true;
    }
  }
  return false;
}

bool LexicalScope::insert(Rpc *r)
{
  std::string temp(r->name());
  std::pair<std::string, std::vector<Parameter*> > p (temp, r->parameters());
  std::pair<std::map<std::pair<std::string, std::vector<Parameter*> >, Rpc*>::iterator, bool> ret;
  ret = this->rpc_definitions_.insert(std::pair<std::pair<std::string, std::vector<Parameter*> >, Rpc*>(p, r));
  return ret.second;
}

bool LexicalScope::insert(Variable *v)
{
  std::string temp(v->identifier());
  std::pair<std::map<std::string,Variable*>::iterator,bool> ret;
  ret = variables_.insert(std::pair<std::string, Variable*>(temp, v));

  // insert into identifier
  this->insert_identifier(v->identifier());
  return ret.second;
}

Variable* LexicalScope::lookup_variable(const char *sym, int* err)
{
  std::string temp(sym);
  if(this->variables_.find(temp) ==  this->variables_.end()) {
    if(this->outer_scope_ == 0x0) {
      *err = 0;
      return 0x0;
    } else {
      return this->outer_scope_->lookup_variable(sym, err);
    }
  }
  else {
    *err = 1;
    return variables_[temp];
  }
}

std::vector<Rpc*> LexicalScope::rpc_in_scope()
{
  printf("rpc in scope lexical scope todo\n");
  std::vector<Rpc*> empty;
  return empty;
}

bool LexicalScope::contains(const char *symbol)
{
  std::string temp(symbol);
  if(this->type_definitions_.find(temp) == this->type_definitions_.end()) {
    if(this->outer_scope_ == 0x0) {
      return false;
    } else {
      return this->outer_scope_->contains(symbol);
    }
  } else {
    return true;
  }

}

// is err needed if we just return null? when would null ever be valid?
Type* LexicalScope::lookup(const char *symbol, int *err)
{
  std::string temp(symbol);
  if(this->type_definitions_.find(temp) ==  this->type_definitions_.end()) {
    if(this->outer_scope_ == 0x0) {
      *err = 0;
      return 0x0;
    } else {
      return this->outer_scope_->lookup(symbol, err);
    }
  }
  else {
    *err = 1;
    printf("In lookup for type %s is %p\n", symbol, type_definitions_[temp]);
    return type_definitions_[temp];
  }
}

bool LexicalScope::insert(const char *symbol, Type *type)
{
  std::string temp(symbol); 
  std::pair<std::map<std::string,Type*>::iterator,bool> ret;
  ret = type_definitions_.insert(std::pair<std::string, Type*>(temp, type));
  printf("In insert pointer for type %s is %p\n", symbol, type);
  return ret.second;
}

void LexicalScope::set_outer_scope(LexicalScope *ls)
{
  this->outer_scope_ = ls;
}

void LexicalScope::add_inner_scope(LexicalScope *ls)
{
  this->inner_scopes_.push_back(ls);
}

void LexicalScope::add_inner_scopes(std::vector<LexicalScope*> scopes)
{
  this->inner_scopes_.insert(this->inner_scopes_.end(), scopes.begin(), scopes.end());
}

std::map<std::string, Type*> LexicalScope::type_definitions()
{
  return this->type_definitions_;
}

std::vector<LexicalScope*> LexicalScope::inner_scopes()
{
  return this->inner_scopes_;
}

LexicalScope* LexicalScope::outer_scope()
{
  return this->outer_scope_;
}

void LexicalScope::resolve_types()
{
  for(std::map<std::string, Type*>::iterator it = this->type_definitions_.begin(); it != this->type_definitions_.end(); it ++) {
    it->second->resolve_types(this);
  }

  for(std::vector<LexicalScope*>::iterator it = this->inner_scopes_.begin(); it != this->inner_scopes_.end(); it ++) {
    LexicalScope *ls = (LexicalScope*) *it;
    ls->resolve_types();
  }
}

void LexicalScope::create_trampoline_structs()
{
  for(std::map<std::string, Type*>::iterator it = this->type_definitions_.begin(); it != this->type_definitions_.end(); it ++) {
    it->second->create_trampoline_structs(this);
  }

  for(std::vector<LexicalScope*>::iterator it = this->inner_scopes_.begin(); it != this->inner_scopes_.end(); it ++) {
    LexicalScope *ls = (LexicalScope*) *it;
    ls->create_trampoline_structs();
  }
}

std::vector<Rpc*> LexicalScope::function_pointer_to_rpc()
{
  std::vector<Rpc*> rpcs;
  for(std::map<std::string, Type*>::iterator it = this->type_definitions_.begin(); it != this->type_definitions_.end(); it ++) {
    Type *t = it->second;

    if(t->num() == 4 || t->num() == 9) { // projection type
      ProjectionType *pt = dynamic_cast<ProjectionType*>(t);
      Assert(pt != 0x0, "Error: dynamic cast to projection type failed!\n");
      std::vector<ProjectionField*> fields = pt->fields();

      for(std::vector<ProjectionField*>::iterator it = fields.begin(); it != fields.end(); it ++) {
	ProjectionField *pf = (ProjectionField*) *it;

	if(pf->type()->num() == 7) { // function pointer field
	  Function *f = dynamic_cast<Function*>(pf->type());
	  rpcs.push_back(f->to_rpc(pt));
	}
      }
    }
    // continue
  }

  for(std::vector<LexicalScope*>::iterator it2 = this->inner_scopes_.begin(); it2 != this->inner_scopes_.end(); it2 ++) {
    LexicalScope *ls = (LexicalScope*) *it2;
    std::vector<Rpc*> tmp_rpcs = ls->function_pointer_to_rpc();
    rpcs.insert(rpcs.end(), tmp_rpcs.begin(), tmp_rpcs.end());
  }
  
  return rpcs;
}

std::map<std::string, Type*> LexicalScope::all_types_outer()
{
  if (this->outer_scope_ == 0x0) {
    return this->type_definitions_;
  }
  
  std::map<std::string, Type*> all_defs (this->type_definitions_);

  std::map<std::string, Type*> outer_defs = this->outer_scope_->all_types_outer();
  all_defs.insert(outer_defs.begin(), outer_defs.end());
  return all_defs;
}

std::map<std::string, Type*> LexicalScope::all_types_inner()
{
  std::map<std::string, Type*> all_defs (this->type_definitions_);

  for(std::vector<LexicalScope*>::iterator it2 = this->inner_scopes_.begin(); it2 != this->inner_scopes_.end(); it2 ++) {
    LexicalScope *ls = (LexicalScope*) *it2;
    std::map<std::string, Type*> tmp = ls->all_types_inner();
    all_defs.insert(tmp.begin(), tmp.end());
  }
  
  return all_defs;
}

/*
 * returns a map of all type definitions, including those from inner scopes
 */
std::map<std::string, Type*> LexicalScope::all_type_definitions()
{
  std::map<std::string, Type*> all_defs (this->type_definitions_);
  
  if(this->outer_scope_ != 0x0) {
    std::map<std::string, Type*> tmp = this->outer_scope_->all_types_outer();
    all_defs.insert(tmp.begin(), tmp.end());
  }
  
  for(std::vector<LexicalScope*>::iterator it2 = this->inner_scopes_.begin(); it2 != this->inner_scopes_.end(); it2 ++) {
    LexicalScope *ls = (LexicalScope*) *it2;
    std::map<std::string, Type*> tmp = ls->all_types_inner();
    all_defs.insert(tmp.begin(), tmp.end());
  }
  
  return all_defs;
}
