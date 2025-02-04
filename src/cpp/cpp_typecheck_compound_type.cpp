/*******************************************************************\

Module: C++ Language Type Checking

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include <util/c_qualifiers.h>
#include <cpp/cpp_convert_type.h>
#include <cpp/cpp_declarator_converter.h>
#include <cpp/cpp_name.h>
#include <cpp/cpp_type2name.h>
#include <cpp/cpp_typecheck.h>
#include <util/arith_tools.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <util/simplify_expr.h>
#include <util/simplify_expr_class.h>

bool cpp_typecheckt::has_const(const typet &type)
{
  if(type.id() == "const")
    return true;
  if(type.id() == "merged_type")
  {
    forall_subtypes(it, type)
      if(has_const(*it))
        return true;

    return false;
  }
  else
    return false;
}

bool cpp_typecheckt::has_volatile(const typet &type)
{
  if(type.id() == "volatile")
    return true;
  if(type.id() == "merged_type")
  {
    forall_subtypes(it, type)
      if(has_volatile(*it))
        return true;

    return false;
  }
  else
    return false;
}

cpp_scopet &cpp_typecheckt::tag_scope(
  const irep_idt &base_name,
  bool has_body,
  bool tag_only_declaration)
{
  // The scope of a compound identifier is difficult,
  // and is different from C.
  //
  // For instance:
  // class A { class B {} }   --> A::B
  // class A { class B; }     --> A::B
  // class A { class B *p; }  --> ::B
  // class B { }; class A { class B *p; } --> ::B
  // class B { }; class A { class B; class B *p; } --> A::B

  // If there is a body, or it's a tag-only declaration,
  // it's always in the current scope, even if we already have
  // it in an upwards scope.

  if(has_body || tag_only_declaration)
    return cpp_scopes.current_scope();

  // No body. Not a tag-only-declaration.
  // Check if we have it already. If so, take it.

  // we should only look for tags, but we don't
  cpp_scopet::id_sett id_set;
  cpp_scopes.current_scope().recursive_lookup(base_name, id_set);

  for(auto it : id_set)
    if(it->is_class())
      return static_cast<cpp_scopet &>(it->get_parent());

  // Tags without body that we don't have already
  // and that are not a tag-only declaration go into
  // the global scope of the namespace.
  return cpp_scopes.current_scope();
}

std::string cpp_typecheckt::fetch_compound_name(const typet &type)
{
  // get the tag name
  bool anonymous = type.find("tag").is_nil();
  std::string identifier, base_name;
  cpp_scopet *dest_scope = nullptr;
  bool has_body = type.body().is_not_nil();
  bool tag_only_declaration = type.get_bool("#tag_only_declaration");

  if(anonymous)
  {
    // Name is unpredictable until it's actually made, sorry.
    return "";
  }

  const cpp_namet &cpp_name = to_cpp_name(type.find("tag"));

  cpp_name.convert(identifier, base_name);

  if(identifier != base_name)
  {
    //err_location(cpp_name.location());
    throw "no namespaces allowed in compound names";
  }

  dest_scope = &const_cast<cpp_typecheckt *>(this)->tag_scope(
    base_name, has_body, tag_only_declaration);

  // See: typecheck_compound_type for full details.
  const std::string symbol_name = dest_scope->prefix + "tag." + identifier;

  return symbol_name;
}

void cpp_typecheckt::typecheck_compound_type(typet &type)
{
  // first save qualifiers
  c_qualifierst qualifiers(type);

  // now clear them from the type
  type.remove("#constant");
  type.remove("#volatile");
  type.remove("#restricted");

  // get the tag name
  bool anonymous = type.find("tag").is_nil();
  std::string identifier, base_name;
  cpp_scopet *dest_scope = nullptr;
  bool has_body = type.body().is_not_nil();
  bool tag_only_declaration = type.get_bool("#tag_only_declaration");

  if(anonymous)
  {
    base_name = identifier =
      "#anon_" + type.id_string() + i2string(anon_counter++);
    type.set("#is_anonymous", true);
    // anonymous structs always go into the current scope
    dest_scope = &cpp_scopes.current_scope();
  }
  else
  {
    const cpp_namet &cpp_name = to_cpp_name(type.find("tag"));

    cpp_name.convert(identifier, base_name);

    if(identifier != base_name)
    {
      err_location(cpp_name.location());
      throw "no namespaces allowed in compound names";
    }

    dest_scope = &tag_scope(base_name, has_body, tag_only_declaration);
  }

  // All types are dumped in the 'cpp' namespace. Types declared in a piece of
  // code with 'c' linkage are no different from C++ types, and otherwise this
  // leads to type conflicts when calling code with a different linkage. (i.e.,
  // a foo pointer isn't compatible with a foo pointer).
  const irep_idt symbol_name = dest_scope->prefix + "tag." + identifier;

  // check if we have it already

  symbolt *previous_symbol = context.find_symbol(symbol_name);
  if(previous_symbol != nullptr)
  {
    // we do!

    symbolt &symbol = *previous_symbol;

    if(has_body)
    {
      if(symbol.type.id() == "incomplete_" + type.id_string())
      {
        // a previously incomplete struct/union becomes complete
        symbol.type.swap(type);
        typecheck_compound_body(symbol);
      }
      else
      {
        err_location(type.location());
        str << "error: struct symbol `" << base_name << "' declared previously"
            << std::endl;
        str << "location of previous definition: " << symbol.location;
        throw 0;
      }
    }
  }
  else
  {
    // produce new symbol
    symbolt symbol;

    symbol.id = symbol_name;
    symbol.name = base_name;
    symbol.value.make_nil();
    symbol.location = type.location();
    symbol.mode = "C++"; // All types are cpp types
    symbol.module = module;
    symbol.type.swap(type);
    symbol.is_type = true;
    symbol.is_macro = false;
    symbol.type.tag(cpp_scopes.current_scope().prefix + id2string(symbol.name));

    // move early, must be visible before doing body
    symbolt *new_symbol;

    if(context.move(symbol, new_symbol))
      throw "cpp_typecheckt::typecheck_compound_type: context.move() failed";

    // put into dest_scope
    cpp_idt &id = cpp_scopes.put_into_scope(*new_symbol);

    id.id_class = cpp_idt::CLASS;
    id.is_scope = true;
    id.prefix =
      cpp_scopes.current_scope().prefix + id2string(new_symbol->name) + "::";
    id.class_identifier = new_symbol->id;
    id.id_class = cpp_idt::CLASS;

    if(has_body)
      typecheck_compound_body(*new_symbol);
    else
    {
      typet new_type("incomplete_" + new_symbol->type.id_string());
      new_type.set("tag", new_symbol->name);
      new_symbol->type.swap(new_type);
    }
  }

  // create type symbol
  typet symbol_type("symbol");
  symbol_type.identifier(symbol_name);
  qualifiers.write(symbol_type);
  type.swap(symbol_type);
}

void cpp_typecheckt::typecheck_compound_declarator(
  const symbolt &symbol,
  const cpp_declarationt &declaration,
  cpp_declaratort &declarator,
  struct_typet::componentst &components,
  const irep_idt &access,
  bool is_static,
  bool is_typedef,
  bool is_mutable)
{
  bool is_cast_operator = declaration.type().id() == "cpp-cast-operator";

  if(is_cast_operator)
  {
    assert(
      declarator.name().get_sub().size() == 2 &&
      declarator.name().get_sub().front().id() == "operator");

    typet type = static_cast<typet &>(declarator.name().get_sub()[1]);
    declarator.type().subtype() = type;

    irept name("name");
    name.identifier("(" + cpp_type2name(type) + ")");
    declarator.name().get_sub().back().swap(name);
  }

  typet final_type = declarator.merge_type(declaration.type());

  typecheck_type(final_type);

  cpp_namet cpp_name;
  cpp_name.swap(declarator.name());

  // Ugly hack
  if(declaration.is_destructor() || declaration.is_constructor())
  {
    // Construtors and destructors can have names with template args
    if(cpp_name.has_template_args())
      cpp_name.get_sub().erase(cpp_name.get_sub().end());
  }

  std::string full_name, base_name;
  cpp_name.convert(full_name, base_name);

  bool is_method = !is_typedef && final_type.id() == "code";
  bool is_constructor = declaration.is_constructor();
  bool is_destructor = declaration.is_destructor();
  bool is_virtual = declaration.member_spec().is_virtual();
  bool is_explicit = declaration.member_spec().is_explicit();
  bool is_inline = declaration.member_spec().is_inline();

  final_type.set("#member_name", symbol.id);

  // first do some sanity checks

  if(is_virtual && !is_method)
  {
    err_location(cpp_name.location());
    str << "only methods can be virtual";
    throw 0;
  }

  if(is_inline && !is_method)
  {
    err_location(cpp_name.location());
    str << "only methods can be inlined";
    throw 0;
  }

  if(is_virtual && is_static)
  {
    err_location(cpp_name.location());
    str << "static methods cannot be virtual";
    throw 0;
  }

  if(is_cast_operator && is_static)
  {
    err_location(cpp_name.location());
    str << "cast operators cannot be static`";
    throw 0;
  }

  if(is_constructor && is_virtual)
  {
    err_location(cpp_name.location());
    str << "constructors cannot be virtual";
    throw 0;
  }

  if(!is_constructor && is_explicit)
  {
    err_location(cpp_name.location());
    str << "only constructors can be explicit";
    throw 0;
  }

  if(is_constructor && base_name != id2string(symbol.name))
  {
    err_location(cpp_name.location());
    str << "member function must return a value or void";
    throw 0;
  }

  if(is_destructor && base_name != "~" + id2string(symbol.name))
  {
    err_location(cpp_name.location());
    str << "destructor with wrong name";
    throw 0;
  }

  // now do actual work

  struct_typet::componentt component;

  irep_idt identifier = cpp_scopes.current_scope().prefix + base_name;

  component.name(identifier);
  component.type() = final_type;
  component.set("access", access);
  component.base_name(base_name);
  component.pretty_name(base_name);
  component.location() = cpp_name.location();

  if(cpp_name.is_operator())
  {
    component.set("is_operator", true);
    component.type().set("#is_operator", true);
  }

  if(is_cast_operator)
    component.set("is_cast_operator", true);

  if(declaration.member_spec().is_explicit())
  {
    component.set("is_explicit", true);
    // Decorate type with it too; it tends to be more accessible than component
    // records during cpp name resolving.
    component.type().set("is_explicit", true);
  }

  // either blank, const, volatile, or const volatile
  const typet &method_qualifier =
    static_cast<const typet &>(declarator.add("method_qualifier"));

  if(is_static)
  {
    component.set("is_static", true);
    component.type().set("#is_static", true);
  }

  if(is_typedef)
    component.set("is_type", true);

  if(is_mutable)
    component.set("is_mutable", true);

  exprt &value = declarator.value();
  irept &initializers = declarator.member_initializers();

  if(is_method)
  {
    component.set("is_inline", declaration.member_spec().is_inline());

    // the 'virtual' name of the function
    std::string virtual_name = component.get_string("base_name") +
                               id2string(function_identifier(
                                 static_cast<const typet &>(component.type())));

    if(has_const(method_qualifier))
      virtual_name += "$const";

    if(has_volatile(method_qualifier))
      virtual_name += "$virtual";

    if(component.type().get("return_type") == "destructor")
      virtual_name = "@dtor";

    // The method may be virtual implicitly.
    std::set<irep_idt> virtual_bases;

    for(struct_typet::componentst::const_iterator it = components.begin();
        it != components.end();
        it++)
    {
      if(it->get_bool("is_virtual"))
      {
        if(it->get("virtual_name") == virtual_name)
        {
          is_virtual = true;
          const code_typet &code_type = to_code_type(it->type());
          assert(code_type.arguments().size() > 0);
          const typet &pointer_type = code_type.arguments()[0].type();
          assert(pointer_type.id() == "pointer");
          virtual_bases.insert(pointer_type.subtype().identifier());
        }
      }
    }

    if(!is_virtual)
    {
      typecheck_member_function(
        symbol.id, component, initializers, method_qualifier, value);

      if(!value.is_nil() && !is_static)
      {
        err_location(cpp_name.location());
        str << "no initialization allowed here";
        throw 0;
      }
    }
    else // virtual
    {
      component.type().set("#is_virtual", true);
      component.type().set("#virtual_name", virtual_name);

      // Check if it is a pure virtual method
      if(is_virtual)
      {
        if(value.is_not_nil() && value.id() == "constant")
        {
          BigInt i;
          to_integer(value, i);
          if(i != 0)
          {
            err_location(declarator.name().location());
            str << "expected 0 to mark pure virtual method, got " << i;
          }
          component.set("is_pure_virtual", true);
          value.make_nil();
        }
      }

      typecheck_member_function(
        symbol.id, component, initializers, method_qualifier, value);

      // get the virtual-table symbol type
      irep_idt vt_name = "virtual_table::" + symbol.id.as_string();

      symbolt *s = context.find_symbol(vt_name);

      if(s == nullptr)
      {
        // first time: create a virtual-table symbol type
        symbolt vt_symb_type;
        vt_symb_type.id = vt_name;
        vt_symb_type.name = "virtual_table::" + symbol.name.as_string();
        vt_symb_type.mode = current_mode;
        vt_symb_type.module = module;
        vt_symb_type.location = symbol.location;
        vt_symb_type.type = struct_typet();
        vt_symb_type.type.set("name", vt_symb_type.id);
        vt_symb_type.is_type = true;

        bool failed = context.move(vt_symb_type);
        assert(!failed);
        (void)failed; //ndebug

        s = context.find_symbol(vt_name);

        // add a virtual-table pointer
        struct_typet::componentt compo;
        compo.type() = pointer_typet(symbol_typet(vt_name));
        compo.set_name(symbol.id.as_string() + "::@vtable_pointer");
        compo.base_name("@vtable_pointer");
        compo.pretty_name(symbol.name.as_string() + "@vtable_pointer");
        compo.set("is_vtptr", true);
        compo.set("access", "public");
        components.push_back(compo);
        put_compound_into_scope(compo);
      }

      assert(s->type.id() == "struct");

      struct_typet &virtual_table = to_struct_type(s->type);

      component.set("virtual_name", virtual_name);
      component.set("is_virtual", is_virtual);

      // add an entry to the virtual table
      struct_typet::componentt vt_entry;
      vt_entry.type() = pointer_typet(component.type());
      vt_entry.set_name(vt_name.as_string() + "::" + virtual_name);
      vt_entry.set("base_name", virtual_name);
      vt_entry.set("pretty_name", virtual_name);
      vt_entry.set("access", "public");
      vt_entry.location() = symbol.location;
      virtual_table.components().push_back(vt_entry);

      // take care of overloading
      while(!virtual_bases.empty())
      {
        irep_idt virtual_base = *virtual_bases.begin();

        // a new function that does 'late casting' of the 'this' parameter
        symbolt func_symb;
        func_symb.id =
          component.get_name().as_string() + "::" + virtual_base.as_string();
        func_symb.name = component.base_name();
        func_symb.mode = current_mode;
        func_symb.module = module;
        func_symb.location = component.location();
        func_symb.type = component.type();

        // change the type of the 'this' pointer
        code_typet &code_type = to_code_type(func_symb.type);
        code_typet::argumentt &arg = code_type.arguments().front();
        arg.type().subtype().set("identifier", virtual_base);

        // create symbols for the arguments
        code_typet::argumentst &args = code_type.arguments();
        for(unsigned i = 0; i < args.size(); i++)
        {
          code_typet::argumentt &arg = args[i];
          irep_idt base_name = arg.get_base_name();

          if(base_name == "")
            base_name = "arg" + i2string(i);

          symbolt arg_symb;
          arg_symb.id = func_symb.id.as_string() + "::" + base_name.as_string();
          arg_symb.name = base_name;
          arg_symb.mode = current_mode;
          arg_symb.location = func_symb.location;
          arg_symb.type = arg.type();

          arg.set("#identifier", arg_symb.id);

          // add the argument to the symbol table
          bool failed = context.move(arg_symb);
          assert(!failed);
          (void)failed; //ndebug
        }

        // do the body of the function
        typecast_exprt late_cast(
          to_code_type(component.type()).arguments()[0].type());

        late_cast.op0() =
          symbol_expr(*namespacet(context).lookup(args[0].cmt_identifier()));

        if(
          code_type.return_type().id() != "empty" &&
          code_type.return_type().id() != "destructor")
        {
          side_effect_expr_function_callt expr_call;
          expr_call.function() =
            symbol_exprt(component.get_name(), component.type());
          expr_call.type() = to_code_type(component.type()).return_type();
          expr_call.arguments().reserve(args.size());
          expr_call.arguments().push_back(late_cast);

          for(unsigned i = 1; i < args.size(); i++)
          {
            expr_call.arguments().push_back(symbol_expr(
              *namespacet(context).lookup(args[i].cmt_identifier())));
          }

          code_returnt code_return;
          code_return.return_value() = expr_call;

          func_symb.value = code_return;
        }
        else
        {
          code_function_callt code_func;
          code_func.function() =
            symbol_exprt(component.get_name(), component.type());
          code_func.arguments().reserve(args.size());
          code_func.arguments().push_back(late_cast);

          for(unsigned i = 1; i < args.size(); i++)
          {
            code_func.arguments().push_back(symbol_expr(
              *namespacet(context).lookup(args[i].cmt_identifier())));
          }

          func_symb.value = code_func;
        }

        // add this new function to the list of components

        struct_typet::componentt new_compo = component;
        new_compo.type() = func_symb.type;
        new_compo.set_name(func_symb.id);
        components.push_back(new_compo);

        // add the function to the symbol table
        {
          bool failed = context.move(func_symb);
          assert(!failed);
          (void)failed; //ndebug
        }

        // next base
        virtual_bases.erase(virtual_bases.begin());
      }
    }
  }

  if(is_static && !is_method) // static non-method member
  {
    // add as global variable to context
    symbolt static_symbol;
    static_symbol.mode = symbol.mode;
    static_symbol.id = identifier;
    static_symbol.type = component.type();
    static_symbol.name = component.base_name();
    static_symbol.lvalue = true;
    static_symbol.static_lifetime = true;
    static_symbol.location = cpp_name.location();
    static_symbol.is_extern = true;

    dinis.push_back(static_symbol.id);

    symbolt *new_symbol;
    if(context.move(static_symbol, new_symbol))
    {
      err_location(cpp_name.location());
      str << "redeclaration of symbol `" << static_symbol.name.as_string()
          << "'";
      throw 0;
    }

    if(value.is_not_nil())
    {
      if(cpp_is_pod(new_symbol->type))
      {
        new_symbol->value.swap(value);
        c_typecheck_baset::do_initializer(*new_symbol);

        // these are macros if they are PODs and come with a (constant) value
        if(new_symbol->type.get_bool("constant"))
        {
          simplify(new_symbol->value);
          new_symbol->is_macro = true;
        }
      }
      else
      {
        symbol_exprt symexpr;
        symexpr.identifier(new_symbol->id);

        exprt::operandst ops;
        ops.push_back(value);
        codet defcode = cpp_constructor(locationt(), symexpr, ops);

        new_symbol->value.swap(defcode);
      }
    }
  }

  // array members must have fixed size
  check_array_types(component.type());

  put_compound_into_scope(component);

  components.push_back(component);
}

void cpp_typecheckt::check_array_types(typet &type)
{
  if(type.id() == "array")
  {
    array_typet &array_type = to_array_type(type);

    if(array_type.size().is_not_nil())
      make_constant_index(array_type.size());

    // recursive call for multi-dimensional arrays
    check_array_types(array_type.subtype());
  }
}

void cpp_typecheckt::put_compound_into_scope(const irept &compound)
{
  const irep_idt &base_name = compound.base_name();
  const irep_idt &name = compound.name();

  // nothing to do if no base_name (e.g., an anonymous bitfield)
  if(base_name == irep_idt())
    return;

  if(compound.type().id() == "code")
  {
    // put the symbol into scope. Template methods need to be put into the
    // class scope, rather than the template, though. Unfortunately, we can't
    // discover that from our argument. So instead attempt to match the scopes
    // in this context to a template mthods.
    cpp_scopet *target_scope = &cpp_scopes.current_scope();
    if(
      target_scope->id_class == cpp_idt::UNKNOWN &&
      target_scope->parents_size() &&
      target_scope->get_parent(0).id_class == cpp_idt::TEMPLATE_SCOPE &&
      target_scope->get_parent(0).get_parent(0).id_class == cpp_idt::CLASS)
      target_scope = &target_scope->get_parent(0).get_parent(0);

    cpp_idt &id = target_scope->insert(base_name);
    id.id_class = compound.is_type() ? cpp_idt::TYPEDEF : cpp_idt::SYMBOL;
    id.identifier = name;
    id.class_identifier = target_scope->identifier;
    id.is_member = true;
    id.is_constructor = compound.type().get("return_type") == "constructor";
    id.is_method = true;
    id.is_static_member = compound.get_bool("is_static");

    // create function block-scope in the scope. Put that in the current scope
    // rather than the class scope.
    cpp_idt &id_block = cpp_scopes.current_scope().insert(
      irep_idt(std::string("$block:") + base_name.c_str()));

    id_block.id_class = cpp_idt::BLOCK_SCOPE;
    id_block.identifier = name;
    id_block.class_identifier = cpp_scopes.current_scope().identifier;
    id_block.is_method = true;
    id_block.is_static_member = compound.get_bool("is_static");

    id_block.is_scope = true;
    id_block.prefix = compound.get_string("prefix");
    cpp_scopes.id_map[id.identifier] = &id_block;
  }
  else
  {
    if(cpp_scopes.current_scope().contains(base_name))
    {
      str << "`" << base_name << "' already in compound scope";
      throw 0;
    }

    // put into the scope
    cpp_idt &id = cpp_scopes.current_scope().insert(base_name);
    id.id_class = compound.is_type() ? cpp_idt::TYPEDEF : cpp_idt::SYMBOL;
    id.identifier = name;
    id.class_identifier = cpp_scopes.current_scope().identifier;
    id.is_member = true;
    id.is_method = false;
    id.is_static_member = compound.get_bool("is_static");
  }
}

void cpp_typecheckt::typecheck_friend_declaration(
  symbolt &symbol,
  cpp_declarationt &declaration)
{
  // A friend of a class can be a function/method,
  // or a struct/class/union type.

  if(declaration.is_template())
  {
    return; // TODO
    err_location(declaration.type().location());
    str << "friend template not supported";
    throw 0;
  }

  // we distinguish these whether there is a declarator
  if(declaration.declarators().empty())
  {
    typet &ftype = declaration.type();

    // must be struct or union
    if(ftype.id() != "struct" && ftype.id() != "union")
    {
      err_location(declaration.type());
      str << "unexpected friend";
      throw 0;
    }

    if(ftype.find("body").is_not_nil())
    {
      err_location(declaration.type());
      str << "friend declaration must not have compound body";
      throw 0;
    }

    // typecheck ftype in the global scope
    cpp_save_scopet saved_scope(cpp_scopes);
    cpp_scopes.go_to_global_scope();

    if(ftype.id() == "struct")
    {
      cpp_namet cpp_name = to_cpp_name(ftype.add("tag"));
      cpp_template_args_non_tct template_args;
      std::string base_name;

      cpp_save_scopet saved_scope(cpp_scopes);

      cpp_typecheck_resolvet resolver(*this);
      resolver.resolve_scope(cpp_name, base_name, template_args);

      if(template_args.is_nil())
      {
        cpp_namet tmp_name;
        tmp_name.get_sub().resize(1);
        tmp_name.get_sub().front().id("name");
        tmp_name.get_sub().front().set("identifier", base_name);
        tmp_name.get_sub().front().add("#location") = cpp_name.location();
        to_cpp_name(ftype.add("tag")).swap(tmp_name);
        typecheck_type(ftype);
        assert(ftype.id() == "symbol");
        symbol.type.add("#friends").move_to_sub(ftype);
      }
      else
      {
        saved_scope.restore();
        // instantiate the template
        ftype.swap(cpp_name);
        typecheck_type(ftype);
        assert(ftype.id() == "symbol");
        symbol.type.add("#friends").move_to_sub(ftype);
      }
    }
    else
    {
      typecheck_type(ftype);

      assert(ftype.id() == "symbol");
      symbol.type.add("#friends").move_to_sub(ftype);
    }

    return;
  }

  // It should be a friend function.
  // Do the declarators.

  Forall_cpp_declarators(sub_it, declaration)
  {
    bool has_value = sub_it->value().is_not_nil();

    if(!has_value)
    {
      // If no value is found, then we jump to the
      // global scope, and we convert the declarator
      // as if it were declared there
      cpp_save_scopet saved_scope(cpp_scopes);
      cpp_scopes.go_to_global_scope();
      cpp_declarator_convertert cpp_declarator_converter(*this);
      const symbolt &conv_symb = cpp_declarator_converter.convert(
        declaration.type(),
        declaration.storage_spec(),
        declaration.member_spec(),
        *sub_it);
      exprt symb_expr = cpp_symbol_expr(conv_symb);
      symbol.type.add("#friends").move_to_sub(symb_expr);
    }
    else
    {
      cpp_declarator_convertert cpp_declarator_converter(*this);
      cpp_declarator_converter.is_friend = true;

      declaration.member_spec().set_inline(true);

      const symbolt &conv_symb = cpp_declarator_converter.convert(
        declaration.type(),
        declaration.storage_spec(),
        declaration.member_spec(),
        *sub_it);

      exprt symb_expr = cpp_symbol_expr(conv_symb);

      symbol.type.add("#friends").move_to_sub(symb_expr);
    }
  }
}

void cpp_typecheckt::typecheck_compound_body(symbolt &symbol)
{
  cpp_save_scopet saved_scope(cpp_scopes);

  // enter scope of compound
  cpp_scopes.set_scope(symbol.id);

  assert(symbol.type.id() == "struct" || symbol.type.id() == "union");

  struct_typet &type = to_struct_type(symbol.type);

  // pull the base types in
  if(!type.find("bases").get_sub().empty())
  {
    if(type.id() == "union")
    {
      err_location(symbol.location);
      throw "union types must not have bases";
    }

    typecheck_compound_bases(type);
  }

  exprt &body = static_cast<exprt &>(type.add("body"));
  struct_typet::componentst &components = type.components();

  symbol.type.set("name", symbol.id);

  // default access
  irep_idt access = type.get_bool("#class") ? "private" : "public";

  bool found_ctor = false;
  bool found_dtor = false;

  // we first do everything but the constructors

  Forall_operands(it, body)
  {
    if(it->id() == "cpp-declaration")
    {
      cpp_declarationt &declaration = to_cpp_declaration(*it);

      if(declaration.member_spec().is_friend())
      {
        typecheck_friend_declaration(symbol, declaration);
        continue; // done
      }

      if(declaration.is_destructor())
        found_dtor = true;

      if(declaration.is_constructor())
        found_ctor = true;

      if(declaration.is_template())
      {
        // remember access mode
        declaration.set("#access", access);
        convert_template_declaration(declaration);
        continue;
      }

      if(declaration.type().id() == "") // empty?
        continue;

      bool is_typedef = convert_typedef(declaration.type());

      // is it tag-only?
      if(
        declaration.type().id() == "struct" ||
        declaration.type().id() == "union" ||
        declaration.type().id() == "c_enum")
        if(declaration.declarators().empty())
          declaration.type().set("#tag_only_declaration", true);

      typecheck_type(declaration.type());

      bool is_static = declaration.storage_spec().is_static();
      bool is_mutable = declaration.storage_spec().is_mutable();

      if(
        declaration.storage_spec().is_extern() ||
        declaration.storage_spec().is_auto() ||
        declaration.storage_spec().is_register())
      {
        err_location(declaration.storage_spec());
        str << "invalid storage class specified for field";
        throw 0;
      }

      typet final_type = follow(declaration.type());

      // anonymous member?
      if(
        declaration.declarators().empty() &&
        final_type.get_bool("#is_anonymous"))
      {
        // we only allow this on struct/union types
        if(final_type.id() != "union" && final_type.id() != "struct")
        {
          err_location(declaration.type());
          throw "member declaration does not declare anything";
        }

        convert_compound_ano_union(declaration, access, components);

        continue;
      }

      // declarators
      Forall_cpp_declarators(d_it, declaration)
      {
        cpp_declaratort &declarator = *d_it;

        // Skip the constructors until all the data members
        // are discovered
        if(declaration.is_constructor())
          continue;

        typecheck_compound_declarator(
          symbol,
          declaration,
          declarator,
          components,
          access,
          is_static,
          is_typedef,
          is_mutable);
      }

      if(declaration.operands().size())
      {
        exprt &value = (exprt &)declaration.op0().add("value");
        exprt &throw_decl = (exprt &)declaration.op0().add("throw_decl");

        // We always insert throw_decl to the begin of the function
        if(throw_decl.statement() == "throw_decl")
        {
          value.operands().insert(value.operands().begin(), throw_decl);

          // Insert flag to end of constructor
          // so we know when to remove throw_decl
          value.operands().push_back(codet("throw_decl_end"));

          // Clear throw_decl
          value.remove("throw_decl");
        }
      }
    }
    else if(it->id() == "cpp-public")
      access = "public";
    else if(it->id() == "cpp-private")
      access = "private";
    else if(it->id() == "cpp-protected")
      access = "protected";
    else
    {
    }
  }

  // If we've seen a constructor, flag this type as not being a POD. This is
  // only useful when we might not be able to work that out later, such as a
  // constructor that gets deleted, or something.
  if(found_ctor || found_dtor)
    type.set("is_not_pod", "1");

  // Add the default dtor, if needed
  // (we have to do the destructor before building the virtual tables,
  //  as the destructor may be virtual!)

  if((found_ctor || !cpp_is_pod(symbol.type)) && !found_dtor)
  {
    // build declaration
    cpp_declarationt dtor;
    default_dtor(symbol, dtor);

    typecheck_compound_declarator(
      symbol,
      dtor,
      dtor.declarators()[0],
      components,
      "public",
      false,
      false,
      false);
  }

  // setup virtual tables before doing the constructors
  do_virtual_table(symbol);

  if(!found_ctor && !cpp_is_pod(symbol.type))
  {
    // it's public!
    exprt cpp_public("cpp-public");
    body.move_to_operands(cpp_public);

    // build declaration
    cpp_declarationt ctor;
    default_ctor(symbol.type.location(), symbol.name, ctor);
    body.add("operands").move_to_sub(ctor);
  }

  // Reset the access type
  access = type.get_bool("#class") ? "private" : "public";

  // All the data members are known now.
  // So let's deal with the constructors that we are given.
  Forall_operands(it, body)
  {
    if(it->id() == "cpp-declaration")
    {
      cpp_declarationt &declaration = to_cpp_declaration(*it);

      if(!declaration.is_constructor())
        continue;

      Forall_cpp_declarators(d_it, declaration)
      {
        cpp_declaratort &declarator = *d_it;

#if 0
        std::string ctor_full_name, ctor_base_name;
        declarator.name().convert(ctor_full_name, ctor_base_name);
#endif

        if(declarator.find("value").is_not_nil())
        {
          if(declarator.find("member_initializers").is_nil())
            declarator.set("member_initializers", "member_initializers");

          check_member_initializers(
            type.add("bases"),
            type.components(),
            declarator.member_initializers());

          full_member_initialization(
            to_struct_type(type), declarator.member_initializers());
        }

        // Finally, we typecheck the constructor with the
        // full member-initialization list
        bool is_static =
          declaration.storage_spec().is_static(); // Shall be false
        bool is_mutable =
          declaration.storage_spec().is_mutable();             // Shall be false
        bool is_typedef = convert_typedef(declaration.type()); // Shall be false

        typecheck_compound_declarator(
          symbol,
          declaration,
          declarator,
          components,
          access,
          is_static,
          is_typedef,
          is_mutable);
      }
    }
    else if(it->id() == "cpp-public")
      access = "public";
    else if(it->id() == "cpp-private")
      access = "private";
    else if(it->id() == "cpp-protected")
      access = "protected";
    else
    {
    }
  }

  if(!cpp_is_pod(symbol.type))
  {
    // Add the default copy constructor
    struct_typet::componentt component;

    if(!find_cpctor(symbol))
    {
      // build declaration
      cpp_declarationt cpctor;
      default_cpctor(symbol, cpctor);
      assert(cpctor.declarators().size() == 1);

      exprt value("cpp_not_typechecked");
      value.copy_to_operands(cpctor.declarators()[0].value());
      cpctor.declarators()[0].value() = value;

      typecheck_compound_declarator(
        symbol,
        cpctor,
        cpctor.declarators()[0],
        components,
        "public",
        false,
        false,
        false);
    }

    // Add the default assignment operator
    if(!find_assignop(symbol))
    {
      // build declaration
      cpp_declarationt assignop;
      default_assignop(symbol, assignop);
      assert(assignop.declarators().size() == 1);

      // The value will be typechecked only if the operator
      // is actually used
      cpp_declaratort declarator;
      assignop.declarators().push_back(declarator);
      assignop.declarators()[0].value() = exprt("cpp_not_typechecked");

      typecheck_compound_declarator(
        symbol,
        assignop,
        assignop.declarators()[0],
        components,
        "public",
        false,
        false,
        false);
    }
  }

  // clean up!
  symbol.type.remove("body");
}

void cpp_typecheckt::move_member_initializers(
  irept &initializers,
  const typet &type,
  exprt &value)
{
  bool is_constructor = type.return_type().id() == "constructor";

  // see if we have initializers
  if(!initializers.get_sub().empty())
  {
    if(!is_constructor)
    {
      err_location(initializers);
      str << "only constructors are allowed to "
             "have member initializers";
      throw 0;
    }

    if(value.is_nil())
    {
      err_location(initializers);
      str << "only constructors with body are allowed to "
             "have member initializers";
      throw 0;
    }

    to_code(value).make_block();

    exprt::operandst::iterator o_it = value.operands().begin();
    forall_irep(it, initializers.get_sub())
    {
      o_it = value.operands().insert(o_it, static_cast<const exprt &>(*it));
      o_it++;
    }
  }
}

void cpp_typecheckt::typecheck_member_function(
  const irep_idt &compound_symbol,
  struct_typet::componentt &component,
  irept &initializers,
  const typet &method_qualifier,
  exprt &value)
{
  symbolt symbol;

  typet &type = component.type();

  if(component.get_bool("is_static"))
  {
    if(method_qualifier.id() != irep_idt())
    {
      err_location(component);
      throw "method is static -- no qualifiers allowed";
    }
  }
  else
  {
    adjust_method_type(compound_symbol, type, method_qualifier);
  }

  if(value.id() == "cpp_not_typechecked")
    move_member_initializers(initializers, type, value.op0());
  else
    move_member_initializers(initializers, type, value);

  irep_idt f_id = function_identifier(component.type());

  const irep_idt identifier = id2string(component.get_name()) + id2string(f_id);

  component.name(identifier);

  component.set(
    "prefix",
    cpp_scopes.current_scope().prefix + component.get_string("base_name") +
      id2string(f_id) + "::");

  if(value.is_not_nil())
    type.set("#inlined", true);

  symbol.id = identifier;
  symbol.name = component.base_name();
  symbol.value.swap(value);
  symbol.mode = current_mode;
  symbol.module = module;
  symbol.type = type;
  symbol.is_type = false;
  symbol.is_macro = false;
  symbol.location = component.location();

  // move early, it must be visible before doing any value
  symbolt *new_symbol;

  if(context.move(symbol, new_symbol))
  {
    err_location(symbol.location);
    str << "failed to insert new symbol: " << symbol.id.c_str() << std::endl;

    symbolt *symb_it = context.find_symbol(symbol.id);

    if(symb_it != nullptr)
    {
      str << "name of previous symbol: " << symbol.id << std::endl;
      str << "location of previous symbol: ";
      err_location(symb_it->location);
    }

    throw 0;
  }

  // remember for later typechecking of body
  add_function_body(new_symbol);
}

void cpp_typecheckt::adjust_method_type(
  const irep_idt &compound_symbol,
  typet &type,
  const typet &method_qualifier)
{
  irept &arguments = type.add("arguments");

  arguments.get_sub().insert(arguments.get_sub().begin(), irept("argument"));

  exprt &argument = static_cast<exprt &>(arguments.get_sub().front());
  argument.type() = typet("pointer");

  argument.type().subtype() = typet("symbol");
  argument.type().subtype().identifier(compound_symbol);

  argument.cmt_identifier("this");
  argument.cmt_base_name("this");

  if(has_const(method_qualifier))
    argument.type().subtype().cmt_constant(true);

  if(has_volatile(method_qualifier))
    argument.type().subtype().cmt_volatile(true);
}

void cpp_typecheckt::add_anonymous_members_to_scope(
  const symbolt &struct_union_symbol)
{
  const struct_union_typet &struct_union_type =
    to_struct_union_type(struct_union_symbol.type);

  const struct_union_typet::componentst &struct_union_components =
    struct_union_type.components();

  // do scoping -- the members of the struct/union
  // should be visible in the containing struct/union,
  // and that recursively!

  for(const auto &struct_union_component : struct_union_components)
  {
    if(struct_union_component.type().id() == "code")
    {
      err_location(struct_union_symbol.type.location());
      str << "anonymous struct/union member `" << struct_union_symbol.name
          << "' shall not have function members";
      throw 0;
    }

    if(struct_union_component.get_anonymous())
    {
      const symbolt *symbol =
        lookup(struct_union_component.type().get("identifier"));
      assert(symbol);
      // recursive call
      add_anonymous_members_to_scope(*symbol);
    }
    else
    {
      const irep_idt &base_name = struct_union_component.base_name();

      if(cpp_scopes.current_scope().contains(base_name))
      {
        err_location(struct_union_component);
        str << "`" << base_name << "' already in scope";
        throw 0;
      }

      cpp_idt &id = cpp_scopes.current_scope().insert(base_name);
      id.id_class = cpp_idt::SYMBOL;
      id.identifier = struct_union_component.name();
      id.class_identifier = struct_union_symbol.id;
      id.is_member = true;
    }
  }
}

void cpp_typecheckt::convert_compound_ano_union(
  const cpp_declarationt &declaration,
  const irep_idt &access,
  struct_typet::componentst &components)
{
  symbolt &struct_union_symbol =
    *context.find_symbol(follow(declaration.type()).name());

  if(
    declaration.storage_spec().is_static() ||
    declaration.storage_spec().is_mutable())
  {
    err_location(struct_union_symbol.type.location());
    throw "storage class is not allowed here";
  }

  if(!cpp_is_pod(struct_union_symbol.type))
  {
    err_location(struct_union_symbol.type.location());
    str << "anonymous struct/union member is not POD";
    throw 0;
  }

  // produce an anonymous member
  irep_idt base_name =
    "#anon_member" + i2string((unsigned long)components.size());

  // All types are in 'cpp' mode.
  irep_idt identifier = cpp_scopes.current_scope().prefix + base_name.c_str();

  typet symbol_type("symbol");
  symbol_type.identifier(struct_union_symbol.id);

  struct_typet::componentt component;
  component.name(identifier);
  component.type() = symbol_type;
  component.set("access", access);
  component.base_name(base_name);
  component.pretty_name(base_name);
  component.set_anonymous(true);
  component.location() = declaration.location();

  components.push_back(component);

  add_anonymous_members_to_scope(struct_union_symbol);

  put_compound_into_scope(component);

  struct_union_symbol.type.set("#unnamed_object", base_name);
}

bool cpp_typecheckt::get_component(
  const locationt &location,
  const exprt &object,
  const irep_idt &component_name,
  exprt &member)
{
  struct_typet final_type = to_struct_type(follow(object.type()));

  const struct_typet::componentst &components = final_type.components();

  for(const auto &component : components)
  {
    exprt tmp("member", component.type());
    tmp.component_name(component.get_name());
    tmp.location() = location;
    tmp.copy_to_operands(object);

    if(component.get_name() == component_name)
    {
      member.swap(tmp);

      bool not_ok = check_component_access(component, final_type);
      if(not_ok)
      {
        if(disable_access_control)
        {
          member.set("#not_accessible", true);
          member.set("#access", component.get("access"));
        }
        else
        {
#if 0
          err_location(location);
          str << "error: member `" << component_name
              << "' is not accessible (" << component.get("access").c_str() <<")";
          str << "\nstruct name: " << final_type.name();
          throw 0;
#endif
        }
      }

      if(object.cmt_lvalue())
        member.set("#lvalue", true);

      if(object.type().cmt_constant() && !component.get_bool("is_mutable"))
        member.type().set("#constant", true);

      member.location() = location;

      return true; // component found
    }
    if(follow(component.type()).find("#unnamed_object").is_not_nil())
    {
      // anonymous union
      assert(follow(component.type()).id() == "union");

      if(get_component(location, tmp, component_name, member))
      {
        if(check_component_access(component, final_type))
        {
          err_location(location);
          str << "error: member `" << component_name << "' is not accessible";
          throw 0;
        }

        if(object.cmt_lvalue())
          member.set("#lvalue", true);

        if(object.cmt_constant() && !component.get_bool("is_mutable"))
          member.type().set("#constant", true);

        member.location() = location;
        return true; // component found
      }
    }
  }

  return false; // component not found
}

bool cpp_typecheckt::check_component_access(
  const irept &component,
  const struct_typet &struct_type)
{
  const irep_idt &access = component.get("access");

  if(access == "noaccess")
    return true; // not ok

  if(access == "public")
    return false; // ok

  assert(access == "private" || access == "protected");

  const irep_idt &struct_identifier = struct_type.name();

  cpp_scopet *pscope = &(cpp_scopes.current_scope());
  while(!(pscope->is_root_scope()))
  {
    if(pscope->is_class())
    {
      if(pscope->identifier == struct_identifier)
        return false; // ok

      const struct_typet &scope_struct =
        to_struct_type(lookup(pscope->identifier)->type);

      if(subtype_typecast(struct_type, scope_struct))
        return false; // ok

      break;
    }
    pscope = &(pscope->get_parent());
  }

  // check friendship
  forall_irep(f_it, struct_type.find("#friends").get_sub())
  {
    const irept &friend_symb = *f_it;

    const cpp_scopet &friend_scope =
      cpp_scopes.get_scope(friend_symb.identifier());

    cpp_scopet *pscope = &(cpp_scopes.current_scope());

    while(!(pscope->is_root_scope()))
    {
      if(friend_scope.identifier == pscope->identifier)
        return false; // ok

      if(pscope->is_class())
        break;

      pscope = &(pscope->get_parent());
    }
  }

  return true; //not ok
}

void cpp_typecheckt::get_bases(
  const struct_typet &type,
  std::set<irep_idt> &set_bases) const
{
  const irept::subt &bases = type.find("bases").get_sub();

  forall_irep(it, bases)
  {
    assert(it->id() == "base");
    assert(it->get("type") == "symbol");

    const struct_typet &base =
      to_struct_type(lookup(it->type().identifier())->type);

    set_bases.insert(base.name());
    get_bases(base, set_bases);
  }
}

void cpp_typecheckt::get_virtual_bases(
  const struct_typet &type,
  std::list<irep_idt> &vbases) const
{
  if(std::find(vbases.begin(), vbases.end(), type.name()) != vbases.end())
    return;

  const irept::subt &bases = type.find("bases").get_sub();

  forall_irep(it, bases)
  {
    assert(it->id() == "base");
    assert(it->get("type") == "symbol");

    const struct_typet &base =
      to_struct_type(lookup(it->type().identifier())->type);

    if(it->get_bool("virtual"))
      vbases.push_back(base.name());

    get_virtual_bases(base, vbases);
  }
}

bool cpp_typecheckt::subtype_typecast(
  const struct_typet &from,
  const struct_typet &to) const
{
  if(from.name() == to.name())
    return true;

  std::set<irep_idt> bases;

  get_bases(from, bases);

  return bases.find(to.name()) != bases.end();
}

void cpp_typecheckt::make_ptr_typecast(exprt &expr, const typet &dest_type)
{
  typet src_type = expr.type();

  assert(src_type.id() == "pointer");
  assert(dest_type.id() == "pointer");

  struct_typet src_struct =
    to_struct_type(static_cast<const typet &>(follow(src_type.subtype())));

  struct_typet dest_struct =
    to_struct_type(static_cast<const typet &>(follow(dest_type.subtype())));

  assert(
    subtype_typecast(src_struct, dest_struct) ||
    subtype_typecast(dest_struct, src_struct));

  expr.make_typecast(dest_type);
}
