/*******************************************************************\

Module: C++ Language Type Checking

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include <cpp/cpp_typecheck.h>
#include <cpp/cpp_util.h>
#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/std_types.h>

codet cpp_typecheckt::cpp_constructor(
  const locationt &location,
  const exprt &object,
  const exprt::operandst &operands)
{
  exprt object_tc = object;

  typecheck_expr(object_tc);

  typet tmp_type(object_tc.type());
  follow_symbol(tmp_type);

  assert(!is_reference(tmp_type));
  if(tmp_type.id() == "array")
  {
    // We allow only one operand and it must be tagged with '#array_ini'.
    // Note that the operand is an array that is used for copy-initialization.
    // In the general case, a program is not allow to use this form of
    // construct. This way of initializing an array is used internaly only.
    // The purpose of the tag #arra_ini is to rule out ill-formed
    // programs.

    if(!operands.empty() && !operands.front().get_bool("#array_ini"))
    {
      err_location(location);
      str << "bad array initializer";
      throw 0;
    }

    assert(operands.empty() || operands.size() == 1);

    if(operands.empty() && cpp_is_pod(tmp_type))
    {
      codet nil;
      nil.make_nil();
      return nil;
    }

    const exprt &size_expr = to_array_type(tmp_type).size();

    if(size_expr.id() == "infinity")
    {
      // don't initialize
      codet nil;
      nil.make_nil();
      return nil;
    }

    BigInt s;
    if(to_integer(size_expr, s))
    {
      err_location(tmp_type);
      str << "array size `" << to_string(size_expr) << "' is not a constant";
      throw 0;
    }

    /*if(cpp_is_pod(tmp_type))
    {
      code_expressiont new_code;
      exprt op_tc = operands.front();
      typecheck_expr(op_tc);
       // Override constantness
      object_tc.type().cmt_constant(false);
      object_tc.set("#lvalue", true);
      side_effect_exprt assign("assign");
      assign.location()=location;
      assign.copy_to_operands(object_tc, op_tc);
      typecheck_side_effect_assignment(assign);
      new_code.move_to_operands(assign);
      return new_code;
    }
    else*/
    {
      codet new_code("block");

      // for each element of the array, call the default constructor
      for(BigInt i = 0; i < s; ++i)
      {
        exprt::operandst tmp_operands;

        exprt constant = from_integer(i, int_type());
        constant.location() = location;

        exprt index("index");
        index.copy_to_operands(object);
        index.copy_to_operands(constant);
        index.location() = location;

        if(!operands.empty())
        {
          exprt operand("index");
          operand.copy_to_operands(operands.front());
          operand.copy_to_operands(constant);
          operand.location() = location;
          tmp_operands.push_back(operand);
        }

        exprt i_code = cpp_constructor(location, index, tmp_operands);

        if(i_code.is_nil())
        {
          new_code.is_nil();
          break;
        }

        new_code.move_to_operands(i_code);
      }
      return new_code;
    }
  }
  else if(cpp_is_pod(tmp_type))
  {
    code_expressiont new_code;
    exprt::operandst operands_tc = operands;

    for(auto &it : operands_tc)
    {
      typecheck_expr(it);
      add_implicit_dereference(it);
    }

    if(operands_tc.size() == 0)
    {
      // a POD is NOT initialized
      new_code.make_nil();
    }
    else if(operands_tc.size() == 1)
    {
      // Override constantness
      object_tc.type().cmt_constant(false);
      object_tc.set("#lvalue", true);
      side_effect_exprt assign("assign");
      assign.location() = location;
      assign.copy_to_operands(object_tc, operands_tc.front());
      typecheck_side_effect_assignment(assign);
      if(new_code.operands().size() == 1)
      {
        // remove zombie operands
        if(new_code.operands().front().id() == "")
          new_code.operands().clear();
      }
      new_code.move_to_operands(assign);
    }
    else
    {
      err_location(location);
      str << "initialization of POD requires one argument, "
             "but got "
          << operands.size() << std::endl;
      throw 0;
    }

    return new_code;
  }
  else if(tmp_type.id() == "union")
  {
    assert(0); // Todo: union
  }
  else if(tmp_type.id() == "struct")
  {
    exprt::operandst operands_tc = operands;

    for(auto &it : operands_tc)
    {
      typecheck_expr(it);
      add_implicit_dereference(it);
    }

    const struct_typet &struct_type = to_struct_type(tmp_type);

    // set most-derived bits
    codet block("block");
    for(const auto &component : struct_type.components())
    {
      if(component.base_name() != "@most_derived")
        continue;

      exprt member("member", bool_typet());
      member.component_name(component.name());
      member.copy_to_operands(object_tc);
      member.location() = location;
      member.set("#lvalue", object_tc.cmt_lvalue());

      exprt val;
      val.make_false();

      if(!component.get_bool("from_base"))
        val.make_true();

      side_effect_exprt assign("assign");
      assign.location() = location;
      assign.move_to_operands(member, val);
      typecheck_side_effect_assignment(assign);
      code_expressiont code_exp;
      code_exp.move_to_operands(assign);
      block.move_to_operands(code_exp);
    }

    // enter struct scope
    cpp_save_scopet save_scope(cpp_scopes);
    cpp_scopes.set_scope(struct_type.name());

    // find name of constructor
    const struct_typet::componentst &components = struct_type.components();

    irep_idt constructor_name;

    for(const auto &component : components)
    {
      const typet &type = component.type();

      if(
        !component.get_bool("from_base") && type.id() == "code" &&
        type.return_type().id() == "constructor")
      {
        constructor_name = component.base_name();
        break;
      }
    }

    // there is always a constructor for non-PODs
    assert(constructor_name != "");

    irept cpp_name("cpp-name");
    cpp_name.get_sub().emplace_back("name");
    cpp_name.get_sub().back().identifier(constructor_name);
    cpp_name.get_sub().back().set("#location", location);

    side_effect_expr_function_callt function_call;
    function_call.location() = location;
    function_call.function().swap(static_cast<exprt &>(cpp_name));
    function_call.arguments().reserve(operands_tc.size());

    for(auto &it : operands_tc)
      function_call.op1().copy_to_operands(it);

    // Decorate function call with the 'this' object. Important so that
    // constructor overloading works. Would add as an argument, but due to
    // overriding of the C version of this method, that causes type horror.
    if(object.id() == "already_typechecked")
    {
      function_call.add("#this_expr") = object.op0();
    }
    else if(object.id() == "symbol")
    {
      // Alas, we need to add a type.
      function_call.add("#this_expr") = object;
      const symbolt &sym = *lookup(object.identifier());
      function_call.add("#this_expr").type() = sym.type;
    }
    else
    {
      assert(object.id() == "index");
      // Also need to extract a type.
      exprt tmp_object = object;
      unsigned int num_subtypes = 0;
      while(tmp_object.id() == "index")
      {
        tmp_object = tmp_object.op0();
        num_subtypes++;
      }

      // We've found the base type. We need to pull the expr type out of that,
      // then attach it to the top level object expr.
      assert(tmp_object.id() == "already_typechecked");
      function_call.add("#this_expr") = object;
      function_call.add("#this_expr").type() = tmp_object.op0().type();

      // Now rectify type to not be an array any more.
      while(num_subtypes-- != 0)
        function_call.add("#this_expr").type() =
          function_call.add("#this_expr").type().subtype();
    }

    // Also, 'this' is an lvalue.
    function_call.add("#this_expr").cmt_lvalue(true);

    typecheck_side_effect_function_call(function_call);
    assert(function_call.statement() == "temporary_object");

    exprt &initializer = static_cast<exprt &>(function_call.add("initializer"));

    assert(
      initializer.id() == "code" && initializer.statement() == "expression");

    side_effect_expr_function_callt &func_ini =
      to_side_effect_expr_function_call(initializer.op0());

    exprt &tmp_this = func_ini.arguments().front();
    assert(
      tmp_this.id() == "address_of" && tmp_this.op0().id() == "new_object");

    exprt address_of("address_of", typet("pointer"));
    address_of.type().subtype() = object_tc.type();
    address_of.copy_to_operands(object_tc);
    tmp_this.swap(address_of);

    if(block.operands().empty())
      return to_code(initializer);

    block.move_to_operands(initializer);
    return block;
  }
  else
    assert(false);

  codet nil;
  nil.make_nil();

  return nil;
}

void cpp_typecheckt::new_temporary(
  const locationt &location,
  const typet &type,
  const exprt::operandst &ops,
  exprt &temporary)
{
  // create temporary object
  exprt tmp_object_expr = exprt("sideeffect", type);
  tmp_object_expr.statement("temporary_object");
  tmp_object_expr.location() = location;

  exprt new_object("new_object");
  new_object.location() = tmp_object_expr.location();
  new_object.set("#lvalue", true);
  new_object.type() = tmp_object_expr.type();

  already_typechecked(new_object);

  codet new_code = cpp_constructor(location, new_object, ops);

  if(new_code.is_not_nil())
  {
    if(new_code.statement() == "assign")
      tmp_object_expr.move_to_operands(new_code.op1());
    else
      tmp_object_expr.add("initializer") = new_code;
  }

  temporary.swap(tmp_object_expr);
}

void cpp_typecheckt::new_temporary(
  const locationt &location,
  const typet &type,
  const exprt &op,
  exprt &temporary)
{
  exprt::operandst ops;
  ops.push_back(op);
  new_temporary(location, type, ops, temporary);
}
