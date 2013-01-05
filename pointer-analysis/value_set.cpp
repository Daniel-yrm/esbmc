/*******************************************************************\

Module: Value Set

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

// XXXjmorse - consider whether or not many more of the type assertions below
// need to take consideration of the data they're working on being an dynamic
// object type, which should match all types.

#include <irep2.h>
#include <migrate.h>

#include <assert.h>

#include <config.h>
#include <context.h>
#include <simplify_expr.h>
#include <expr_util.h>
#include <base_type.h>
#include <std_expr.h>
#include <i2string.h>
#include <prefix.h>
#include <std_code.h>
#include <arith_tools.h>
#include <pointer_offset_size.h>

#include <langapi/language_util.h>
#include <ansi-c/c_types.h>

#include "value_set.h"

const value_sett::object_map_dt value_sett::object_map_dt::empty;
object_numberingt value_sett::object_numbering;

void value_sett::output(
  const namespacet &ns,
  std::ostream &out) const
{
  for(valuest::const_iterator
      v_it=values.begin();
      v_it!=values.end();
      v_it++)
  {
    std::string identifier, display_name;
    
    const entryt &e=v_it->second;
  
    if(has_prefix(e.identifier, "value_set::dynamic_object"))
    {
      display_name=e.identifier + e.suffix;
      identifier="";
    }
    else if(e.identifier=="value_set::return_value")
    {
      display_name="RETURN_VALUE"+e.suffix;
      identifier="";
    }
    else
    {
      #if 0
      const symbolt &symbol=ns.lookup(e.identifier);
      display_name=symbol.display_name()+e.suffix;
      identifier=symbol.name;
      #else
      identifier = e.identifier;
      display_name = identifier + e.suffix;
      #endif
    }
    
    out << display_name;

    out << " = { ";

    const object_map_dt &object_map=e.object_map.read();
    
    unsigned width=0;
    
    for(object_map_dt::const_iterator
        o_it=object_map.begin();
        o_it!=object_map.end();
        o_it++)
    {
      const expr2tc &o = object_numbering[o_it->first];
    
      std::string result;

      if (is_invalid2t(o) || is_unknown2t(o))
        result=from_expr(ns, identifier, o);
      else
      {
        result="<"+from_expr(ns, identifier, o)+", ";
      
        if(o_it->second.offset_is_set)
          result+=integer2string(o_it->second.offset)+"";
        else
          result+="*";
        
        result += ", "+from_type(ns, identifier, o->type);
      
        result+=">";
      }

      out << result;

      width+=result.size();
    
      object_map_dt::const_iterator next(o_it);
      next++;

      if(next!=object_map.end())
      {
        out << ", ";
        if(width>=40) out << "\n      ";
      }
    }

    out << " } " << std::endl;
  }
}

expr2tc
value_sett::to_expr(object_map_dt::const_iterator it) const
{
  const expr2tc &object = object_numbering[it->first];
  
  if (is_invalid2t(object) || is_unknown2t(object))
    return object;

  expr2tc offs;
  if (it->second.offset_is_set)
    offs = constant_int2tc(index_type2(), it->second.offset);
  else
    offs = unknown2tc(index_type2());

  expr2tc obj = object_descriptor2tc(object->type, object, offs);
  return obj;
}

bool value_sett::make_union(const value_sett::valuest &new_values, bool keepnew)
{
  bool result=false;
  
  for(valuest::const_iterator
      it=new_values.begin();
      it!=new_values.end();
      it++)
  {
    valuest::iterator it2=values.find(it->first);

    if(it2==values.end())
    {
      // we always track these
      if(has_prefix(id2string(it->second.identifier),
           "value_set::dynamic_object") ||
         it->second.identifier=="value_set::return_value" ||
         keepnew)
      {
        values.insert(*it);
        result=true;
      }

      continue;
    }
      
    entryt &e=it2->second;
    const entryt &new_e=it->second;
    
    if(make_union(e.object_map, new_e.object_map))
      result=true;
  }
  
  return result;
}

bool value_sett::make_union(object_mapt &dest, const object_mapt &src) const
{
  bool result=false;
  
  for(object_map_dt::const_iterator it=src.read().begin();
      it!=src.read().end();
      it++)
  {
    if(insert(dest, it))
      result=true;
  }
  
  return result;
}

void value_sett::get_value_set(
  const expr2tc &expr,
  value_setst::valuest &dest,
  const namespacet &ns) const
{
  object_mapt object_map;
  get_value_set(expr, object_map, ns);
  
  for(object_map_dt::const_iterator
      it=object_map.read().begin();
      it!=object_map.read().end();
      it++)
    dest.push_back(to_expr(it));
}

void value_sett::get_value_set(
  const expr2tc &expr,
  object_mapt &dest,
  const namespacet &ns) const
{
  expr2tc new_expr = expr->simplify();
  if (is_nil_expr(new_expr))
    new_expr = expr;

  // Otherwise, continue on as normal.
  get_value_set_rec(new_expr, dest, "", new_expr->type, ns);
}

void value_sett::get_value_set_rec(
  const expr2tc &expr,
  object_mapt &dest,
  const std::string &suffix,
  const type2tc &original_type,
  const namespacet &ns) const
{

  if (is_unknown2t(expr) || is_invalid2t(expr))
  {
    insert(dest, unknown2tc(original_type));
    return;
  }
  else if (is_index2t(expr))
  {
    const index2t &idx = to_index2t(expr);
    const type2tc &source_type = idx.source_value->type;

    assert(is_array_type(source_type) || is_string_type(source_type));
           
    get_value_set_rec(idx.source_value, dest, "[]"+suffix, original_type, ns);
    
    return;
  }
  else if (is_member2t(expr))
  {
    const member2t &memb = to_member2t(expr);
    const type2tc &source_type = memb.source_value->type;

    assert(is_struct_type(source_type) || is_union_type(source_type));
           
    get_value_set_rec(memb.source_value, dest,
                      "." + memb.member.as_string() + suffix,
                      original_type, ns);
    return;
  }
  else if (is_symbol2t(expr))
  {
    const symbol2t &sym = to_symbol2t(expr);

    if (sym.thename == "NULL" && is_pointer_type(expr))
    {
      // XXXjmorse - looks like there's no easy way to avoid this ns.follow
      // for getting the null objects type, without an internal pointer
      // repr reshuffle.
      const pointer_type2t &ptr_ref = to_pointer_type(expr->type);
      typet subtype = migrate_type_back(ptr_ref.subtype);
      if (subtype.id() == "symbol")
        subtype = ns.follow(subtype);

      expr2tc tmp = null_object2tc(ptr_ref.subtype);
      insert(dest, tmp, 0);
      return;
    }

    // look it up
    valuest::const_iterator v_it = values.find(string_wrapper(sym.get_symbol_name() + suffix));
      
    if(v_it!=values.end())
    {
      make_union(dest, v_it->second.object_map);
      return;
    }
  }
  else if (is_if2t(expr))
  {
    const if2t &ifval = to_if2t(expr);

    get_value_set_rec(ifval.true_value, dest, suffix, original_type, ns);
    get_value_set_rec(ifval.false_value, dest, suffix, original_type, ns);

    return;
  }
  else if (is_address_of2t(expr))
  {
    const address_of2t &addrof = to_address_of2t(expr);
    get_reference_set(addrof.ptr_obj, dest, ns);
    return;
  }
  else if (is_dereference2t(expr))
  {
    object_mapt reference_set;
    get_reference_set(expr, reference_set, ns);
    const object_map_dt &object_map=reference_set.read();
    
    if(object_map.begin()!=object_map.end())
    {
      for(object_map_dt::const_iterator
          it1=object_map.begin();
          it1!=object_map.end();
          it1++)
      {
        const expr2tc &object = object_numbering[it1->first];
        get_value_set_rec(object, dest, suffix, original_type, ns);
      }

      return;
    }
  }
  else if(is_constant_expr(expr))
  {
    // Check for null moved to symbol2t.
    return;
  }
  else if (is_typecast2t(expr))
  {
    const typecast2t &cast = to_typecast2t(expr);
    get_value_set_rec(cast.from, dest, suffix, original_type, ns);
    return;
  }
  else if (is_add2t(expr) || is_sub2t(expr))
  {
    if (is_pointer_type(expr))
    {
      // find the pointer operand
      // XXXjmorse - polymorphism.
      const expr2tc &op0 = (is_add2t(expr))
                           ? to_add2t(expr).side_1
                           : to_sub2t(expr).side_1;
      const expr2tc &op1 = (is_add2t(expr))
                           ? to_add2t(expr).side_2
                           : to_sub2t(expr).side_2;

      assert(!(is_pointer_type(op0) && is_pointer_type(op1)) &&
              "Cannot have pointer arithmatic with two pointers as operands");

      const expr2tc &ptr_op= (is_pointer_type(op0)) ? op0 : op1;
      const expr2tc &non_ptr_op= (is_pointer_type(op0)) ? op1 : op0;

      object_mapt pointer_expr_set;
      get_value_set_rec(ptr_op, pointer_expr_set, "", ptr_op->type, ns);

      // Calculate the offset caused by this addition, in _bytes_. Involves
      // pointer arithmetic. We also use the _perceived_ type of what we're
      // adding or subtracting from/to, it might be being typecasted.
      const type2tc &subtype = to_pointer_type(ptr_op->type).subtype;
      mp_integer total_offs(0);
      bool is_const = false;
      try {
        if (is_constant_int2t(non_ptr_op)) {
          if (to_constant_int2t(non_ptr_op).constant_value.is_zero()) {
            total_offs = 0;
          } else {
            // Potentially rename,
            const type2tc renamed = ns.follow(subtype);
            mp_integer elem_size = pointer_offset_size(*renamed);
            const mp_integer &val =to_constant_int2t(non_ptr_op).constant_value;
            total_offs = val * elem_size;
            if (is_sub2t(expr))
              total_offs.negate();
          }
          is_const = true;
        } else {
          is_const = false;
        }
      } catch (array_type2t::dyn_sized_array_excp *e) { // Nondet'ly sized.
      } catch (array_type2t::inf_sized_array_excp *e) {
      } catch (type2t::symbolic_type_excp *e) {
        // This vastly annoying piece of code is making operations on void
        // pointers, or worse. If a void pointer, treat the multiplier of the
        // addition as being one. If not void pointer, throw cookies.
        if (is_empty_type(subtype)) {
          total_offs = to_constant_int2t(non_ptr_op).constant_value;
          is_const = true;
        } else {
          std::cerr << "Pointer arithmetic on type where we can't determine ";
          std::cerr << "size:" << std::endl;
          std::cerr << subtype->pretty(0) << std::endl;
          abort();
        }
      }

      for(object_map_dt::const_iterator
          it=pointer_expr_set.read().begin();
          it!=pointer_expr_set.read().end();
          it++)
      {
        objectt object=it->second;

        if (is_const && object.offset_is_set) {
          // Both are const; we can accumulate offsets;
          object.offset += total_offs;
        } else {
          object.offset_is_set=false;
        }

        insert(dest, it->first, object);
      }

      return;
    }
  }
  else if (is_sideeffect2t(expr))
  {
    const sideeffect2t &side = to_sideeffect2t(expr);
    switch (side.kind) {
    case sideeffect2t::malloc:
      {
      assert(suffix=="");
      const type2tc &dynamic_type = side.alloctype;


      expr2tc locnum = gen_uint(location_number);
      dynamic_object2tc dynobj(dynamic_type, locnum, false, false);

      insert(dest, dynobj, 0);
      }
      return;          
 
    case sideeffect2t::cpp_new:
    case sideeffect2t::cpp_new_arr:
      {
      assert(suffix=="");
      assert(is_pointer_type(side.type));

      expr2tc locnum = gen_uint(location_number);

      const pointer_type2t &ptr = to_pointer_type(side.type);

      dynamic_object2tc dynobj(ptr.subtype, locnum, false, false);

      insert(dest, dynobj, 0);
      }
      return;
    case sideeffect2t::nondet:
      // XXXjmorse - don't know what to do here, previously wasn't handled,
      // so I won't try to handle it now.
      return;
    default:
      std::cerr << "Unexpected side-effect: " << expr->pretty(0) << std::endl;
      abort();
    }
  }
  else if (is_constant_struct2t(expr))
  {
    // this is like a static struct object
    address_of2tc tmp(expr->type, expr);
    insert(dest, tmp, 0);
    return;
  }
  else if (is_with2t(expr))
  {
    const with2t &with = to_with2t(expr);

    // this is the array/struct
    object_mapt tmp_map0;
    get_value_set_rec(with.source_value, tmp_map0, suffix, original_type, ns);

    // this is the update value -- note NO SUFFIX
    object_mapt tmp_map2;
    get_value_set_rec(with.update_value, tmp_map2, "", original_type, ns);

    make_union(dest, tmp_map0);
    make_union(dest, tmp_map2);
  }
  else if (is_constant_array_of2t(expr) || is_constant_array2t(expr))
  {
    // these are supposed to be done by assign()
    assert(0 && "Encountered array irep in get_value_set_rec");
  }
  else if (is_dynamic_object2t(expr))
  {
    const dynamic_object2t &dyn = to_dynamic_object2t(expr);
  
    // XXXjmorse, could become a uint.
    assert(is_constant_int2t(dyn.instance));
    const constant_int2t &intref = to_constant_int2t(dyn.instance);
    std::string idnum = integer2string(intref.constant_value);
    const std::string name = "value_set::dynamic_object" + idnum + suffix;
  
    // look it up
    valuest::const_iterator v_it=values.find(string_wrapper(name));

    if(v_it!=values.end())
    {
      make_union(dest, v_it->second.object_map);
      return;
    }
  }

  unknown2tc tmp(original_type);
  insert(dest, tmp);
}

void value_sett::get_reference_set(
  const expr2tc &expr,
  value_setst::valuest &dest,
  const namespacet &ns) const
{
  object_mapt object_map;
  get_reference_set(expr, object_map, ns);
  
  for(object_map_dt::const_iterator
      it=object_map.read().begin();
      it!=object_map.read().end();
      it++)
    dest.push_back(to_expr(it));
}

void value_sett::get_reference_set_rec(
  const expr2tc &expr,
  object_mapt &dest,
  const namespacet &ns) const
{

  if (is_symbol2t(expr) || is_dynamic_object2t(expr) ||
      is_constant_string2t(expr))
  {
    if (is_array_type(expr) &&
        is_array_type(to_array_type(expr->type).subtype))
      insert(dest, expr);
    else    
      insert(dest, expr, 0);

    return;
  }
  else if (is_dereference2t(expr))
  {
    const dereference2t &deref = to_dereference2t(expr);
    get_value_set_rec(deref.value, dest, "", deref.type, ns);
    return;
  }
  else if (is_index2t(expr))
  {
    const index2t &index = to_index2t(expr);
    
    assert(is_array_type(index.source_value) ||
           is_string_type(index.source_value));
    
    object_mapt array_references;
    get_reference_set(index.source_value, array_references, ns);
        
    const object_map_dt &object_map=array_references.read();
    
    for(object_map_dt::const_iterator
        a_it=object_map.begin();
        a_it!=object_map.end();
        a_it++)
    {
      expr2tc object = object_numbering[a_it->first];

      if (is_unknown2t(object)) {
        unknown2tc unknown(expr->type);
        insert(dest, unknown);
      } else if (is_array_type(object) || is_string_type(object)) {
        index2tc new_index(index.type, object, zero_uint);
        
        // adjust type?
        if (object->type != index.source_value->type) {
          object = typecast2tc(index.source_value->type, object);
          new_index = index2tc(index.type, object, zero_uint);
        }

        objectt o = a_it->second;

        if (is_constant_int2t(index.index) &&
            to_constant_int2t(index.index).constant_value.is_zero())
          ;
        else if (is_constant_int2t(index.index) && o.offset_is_zero())
          o.offset = to_constant_int2t(index.index).constant_value;
        else
          o.offset_is_set = false;
          
        insert(dest, new_index, o);
      } else {
        std::cerr << "Unexpected type id " << get_type_id(object->type)
                  << " in get_reference_set index handler" << std::endl;
        abort();
      }
    }
    
    return;
  }
  else if (is_member2t(expr))
  {
    const member2t &memb = to_member2t(expr);

    object_mapt struct_references;
    get_reference_set(memb.source_value, struct_references, ns);
    
    const object_map_dt &object_map=struct_references.read();

    for(object_map_dt::const_iterator
        it=object_map.begin();
        it!=object_map.end();
        it++)
    {
      expr2tc object = object_numbering[it->first];
      
      if (is_unknown2t(object)) {
        unknown2tc unknown(memb.type);
        insert(dest, unknown);
      } else {
        objectt o=it->second;

        member2tc new_memb(memb.type, object,memb.member);
        
        // adjust type?
        if (memb.source_value->type != object->type) {
          object = typecast2tc(memb.source_value->type, object);
          new_memb = member2tc(memb.type, object, memb.member);
        }
        
        insert(dest, new_memb, o);
      }
    }

    return;
  }
  else if (is_if2t(expr))
  {
    const if2t &anif = to_if2t(expr);
    get_reference_set_rec(anif.true_value, dest, ns);
    get_reference_set_rec(anif.false_value, dest, ns);
    return;
  }
  else if (is_typecast2t(expr))
  {
    const typecast2t &cast = to_typecast2t(expr);
    get_reference_set_rec(cast.from, dest, ns);
    return;
  }
  else if (is_byte_extract2t(expr))
  {
    // Address of byte extracts can refer to the object that is being extracted
    // from.
    const byte_extract2t &extract = to_byte_extract2t(expr);

    // This may or may not have a constant offset
    objectt o;
    if (is_constant_int2t(extract.source_offset)) {
      o.offset = to_constant_int2t(extract.source_offset).constant_value;
      o.offset_is_set = true;
    } else {
      o.offset_is_set = false;
    }

    insert(dest, extract.source_value, o);
    return;
  }

  unknown2tc unknown(expr->type);
  insert(dest, unknown);
}

void value_sett::assign(
  const expr2tc &lhs,
  const expr2tc &rhs,
  const namespacet &ns,
  bool add_to_sets)
{

  if (is_if2t(rhs))
  {
    const if2t &ifref = to_if2t(rhs);
    assign(lhs, ifref.true_value, ns, add_to_sets);
    assign(lhs, ifref.false_value, ns, true);
    return;
  }

  assert(!is_symbol_type(lhs));
  const type2tc &lhs_type = lhs->type;
  
  if (is_struct_type(lhs_type) || is_union_type(lhs_type))
  {
    const std::vector<type2tc> &members = (is_struct_type(lhs_type))
      ? to_struct_type(lhs_type).members : to_union_type(lhs_type).members;
    const std::vector<irep_idt> &member_names = (is_struct_type(lhs_type))
      ? to_struct_type(lhs_type).member_names
      : to_union_type(lhs_type).member_names;
    
    unsigned int i = 0;
    for (std::vector<type2tc>::const_iterator c_it = members.begin();
        c_it != members.end(); c_it++, i++)
    {
      const type2tc &subtype = *c_it;
      const irep_idt &name = member_names[i];

      // ignore methods
      if (is_code_type(subtype))
        continue;
    
      member2tc lhs_member(subtype, lhs, name);

      expr2tc rhs_member;
      if (is_unknown2t(rhs))
      {
        rhs_member = unknown2tc(subtype);
      }
      else if (is_invalid2t(rhs))
      {
        rhs_member = invalid2tc(subtype);
      }
      else
      {
        if (is_index2t(rhs)) {
          if (is_symbol2t(lhs)) {
            assign(lhs_member, to_index2t(rhs).source_value, ns, add_to_sets);
            return;
    	  }
    	}

        assert(base_type_eq(rhs->type, lhs_type, ns));
        expr2tc rhs_member = make_member(rhs, name, ns);
        assign(lhs_member, rhs_member, ns, add_to_sets);
      }
    }
  }
  else if (is_array_type(lhs_type))
  {
    const array_type2t &arr_type = to_array_type(lhs_type);
    unknown2tc unknown(index_type2());
    index2tc lhs_index(arr_type.subtype, lhs, unknown);

    if (is_unknown2t(rhs) || is_invalid2t(rhs))
    {
      // XXXjmorse - was passing rhs as exprt(rhs.id(), type.subtype()),
      // Which discards much data and is probably invalid.
      assign(lhs_index, rhs, ns, add_to_sets);
    }
    else
    {
      assert(base_type_eq(rhs->type, lhs_type, ns));
        
      if (is_constant_array_of2t(rhs))
      {
        assign(lhs_index, to_constant_array_of2t(rhs).initializer,
               ns, add_to_sets);
      }
      else if (is_constant_array2t(rhs) || is_constant_expr(rhs))
      {
        // ...whattt
#if 0
        forall_operands(o_it, rhs)
        {
          assign(lhs_index, *o_it, ns, add_to_sets);
          add_to_sets=true;
        }
#endif
        forall_operands2(it, expr_list, rhs) {
          assign(lhs_index, **it, ns, add_to_sets);
          add_to_sets = true;
        }
      }
      else if (is_with2t(rhs))
      {
        const with2t &with = to_with2t(rhs);

        unknown2tc unknown(index_type2());
        index2tc idx(arr_type.subtype, with.source_value, unknown);

        assign(lhs_index, idx, ns, add_to_sets);
        assign(lhs_index, with.update_value, ns, true);
      }
      else
      {
        unknown2tc unknown(index_type2());
        index2tc rhs_idx(arr_type.subtype, rhs, unknown);
        assign(lhs_index, rhs_idx, ns, true);
      }
    }
  }
  else
  {
    // basic type
    object_mapt values_rhs;
    
    get_value_set(rhs, values_rhs, ns);
    
    assign_rec(lhs, values_rhs, "", ns, add_to_sets);
  }
}

void value_sett::do_free(
  const expr2tc &op,
  const namespacet &ns)
{
  // op must be a pointer
  assert(is_pointer_type(op));

  // find out what it points to    
  object_mapt value_set;
  get_value_set(op, value_set, ns);
  
  const object_map_dt &object_map=value_set.read();
  
  // find out which *instances* interest us
  expr_sett to_mark;
  
  for(object_map_dt::const_iterator
      it=object_map.begin();
      it!=object_map.end();
      it++)
  {
    const expr2tc &object = object_numbering[it->first];

    if (is_dynamic_object2t(object))
    {
      const dynamic_object2t &dynamic_object = to_dynamic_object2t(object);
      
      if (!dynamic_object.invalid) {
        to_mark.insert(dynamic_object.instance);
      }
    }
  }
  
  // mark these as 'may be invalid'
  // this, unfortunately, destroys the sharing
  for(valuest::iterator v_it=values.begin();
      v_it!=values.end();
      v_it++)
  {
    object_mapt new_object_map;

    const object_map_dt &old_object_map=
      v_it->second.object_map.read();
      
    bool changed=false;
    
    for(object_map_dt::const_iterator
        o_it=old_object_map.begin();
        o_it!=old_object_map.end();
        o_it++)
    {
      const expr2tc &object = object_numbering[o_it->first];

      if (is_dynamic_object2t(object))
      {
        const expr2tc &instance = to_dynamic_object2t(object).instance;

        if (to_mark.count(instance) == 0)
          set(new_object_map, o_it);
        else
        {
          // adjust
          objectt o=o_it->second;
          dynamic_object2tc new_dyn(object);
          new_dyn.get()->invalid = false;
          new_dyn.get()->unknown = true;
          insert(new_object_map, new_dyn, o);
          changed=true;
        }
      }
      else
        set(new_object_map, o_it);
    }
    
    if(changed)
      v_it->second.object_map=new_object_map;
  }
}

void value_sett::assign_rec(
  const expr2tc &lhs,
  const object_mapt &values_rhs,
  const std::string &suffix,
  const namespacet &ns,
  bool add_to_sets)
{

  if (is_symbol2t(lhs))
  {
    std::string identifier = to_symbol2t(lhs).get_symbol_name();
    
    if(add_to_sets)
      make_union(get_entry(identifier, suffix).object_map, values_rhs);
    else
      get_entry(identifier, suffix).object_map=values_rhs;
  }
  else if (is_dynamic_object2t(lhs))
  {
    const dynamic_object2t &dynamic_object = to_dynamic_object2t(lhs);
  
    if (is_unknown2t(dynamic_object.instance))
      return; // XXXjmorse - we're assigning to something unknown.
              // Not much we can do about it.
    assert(is_constant_int2t(dynamic_object.instance));
    unsigned int idnum =
      to_constant_int2t(dynamic_object.instance).constant_value.to_long();
    const std::string name = "value_set::dynamic_object" + i2string(idnum);

    make_union(get_entry(name, suffix).object_map, values_rhs);
  }
  else if (is_dereference2t(lhs))
  {
    object_mapt reference_set;
    get_reference_set(lhs, reference_set, ns);

    if(reference_set.read().size()!=1)
      add_to_sets=true;
      
    for(object_map_dt::const_iterator
        it=reference_set.read().begin();
        it!=reference_set.read().end();
        it++)
    {
      // XXXjmorse - some horrible type safety is about to fail
      const expr2tc obj = object_numbering[it->first];

      if (!is_unknown2t(obj))
        assign_rec(obj, values_rhs, suffix, ns, add_to_sets);
    }
  }
  else if (is_index2t(lhs))
  {
    assert(is_array_type(to_index2t(lhs).source_value) ||
           is_string_type(to_index2t(lhs).source_value) ||
           is_dynamic_object2t(to_index2t(lhs).source_value));

    assign_rec(to_index2t(lhs).source_value, values_rhs, "[]"+suffix, ns, true);
  }
  else if (is_member2t(lhs))
  {
    type2tc tmp;
    const member2t &member = to_member2t(lhs);
    const std::string &component_name = member.member.as_string();

    // Might travel through a dereference, in which case type resolving is
    // required
    const type2tc *ourtype = &member.source_value->type;
    if (is_symbol_type(*ourtype)) {
      tmp = ns.follow(*ourtype);
      ourtype = &tmp;
    }

    assert(is_struct_type(*ourtype) || is_union_type(*ourtype) ||
           is_dynamic_object2t(member.source_value));
           
    assign_rec(to_member2t(lhs).source_value, values_rhs,
               "."+component_name+suffix, ns, add_to_sets);
  }
  else if (is_zero_string2t(lhs) || is_zero_length_string2t(lhs) ||
           is_constant_string2t(lhs) || is_null_object2t(lhs) ||
           is_valid_object2t(lhs) || is_deallocated_obj2t(lhs) ||
           is_dynamic_size2t(lhs))
  {
    // Ignored
  }
  else if (is_typecast2t(lhs))
  {
    assign_rec(to_typecast2t(lhs).from, values_rhs, suffix, ns, add_to_sets);
  }
  else if (is_byte_extract2t(lhs))
  {
    assign_rec(to_byte_extract2t(lhs).source_value, values_rhs, suffix,
               ns, true);
  }
  else
    throw "assign NYI: `" + get_expr_id(lhs)+ "'";
}

void value_sett::do_function_call(
  const irep_idt &function,
  const std::vector<expr2tc> &arguments,
  const namespacet &ns)
{
  const symbolt &symbol=ns.lookup(function);

  const code_typet &type=to_code_type(symbol.type);

  type2tc tmp_migrated_type;
  migrate_type(type, tmp_migrated_type);
  const code_type2t &migrated_type =
    dynamic_cast<const code_type2t &>(*tmp_migrated_type.get());

  const std::vector<type2tc> &argument_types = migrated_type.arguments;
  const std::vector<irep_idt> &argument_names = migrated_type.argument_names;

  // these first need to be assigned to dummy, temporary arguments
  // and only thereafter to the actuals, in order
  // to avoid overwriting actuals that are needed for recursive
  // calls

  for(unsigned i=0; i<arguments.size(); i++)
  {
    const std::string identifier="value_set::dummy_arg_"+i2string(i);
    add_var(identifier, "");

    expr2tc dummy_lhs;
    expr2tc tmp_arg = arguments[i];
    if (is_nil_expr(tmp_arg)) {
      // As a workaround for the "--function" option, which feeds "nil"
      // arguments in here, take the expected function argument type rather
      // than the type from the argument.
      tmp_arg = unknown2tc(argument_types[i]);
      dummy_lhs = symbol2tc(argument_types[i], identifier);
    } else {
      dummy_lhs = symbol2tc(arguments[i]->type, identifier);
    }

    assign(dummy_lhs, tmp_arg, ns, true);
  }

  // now assign to 'actual actuals'

  unsigned i=0;

  std::vector<type2tc>::const_iterator it2 = argument_types.begin();
  for (std::vector<irep_idt>::const_iterator it = argument_names.begin();
      it != argument_names.end(); it++, it2++)
  {
    const std::string &identifier = it->as_string();
    if(identifier=="") continue;

    add_var(identifier, "");
  
    symbol2tc v_expr(*it2, "value_set::dummy_arg_"+i2string(i));

    symbol2tc actual_lhs(*it2, identifier);
    assign(actual_lhs, v_expr, ns, true);
    i++;
  }
}

void value_sett::do_end_function(
  const expr2tc &lhs,
  const namespacet &ns)
{
  if (is_nil_expr(lhs))
    return;

  symbol2tc rhs(lhs->type, irep_idt("value_set::return_value"));

  assign(lhs, rhs, ns);
}

void value_sett::apply_code(
  const expr2tc &code,
  const namespacet &ns)
{

  if (is_code_block2t(code))
  {
    const code_block2t &ref = to_code_block2t(code);
    forall_exprs(it, ref.operands)
      apply_code(*it, ns);
  }
  else if (is_code_assign2t(code))
  {
    const code_assign2t &ref = to_code_assign2t(code);
    assign(ref.target, ref.source, ns);
  }
  else if (is_code_init2t(code))
  {
    const code_init2t &ref = to_code_init2t(code);
    assign(ref.target, ref.source, ns);
  }
  else if (is_code_decl2t(code))
  {
    const code_decl2t &ref = to_code_decl2t(code);
    symbol2tc sym(ref.type, ref.value);
    invalid2tc invalid(ref.type);
    assign(sym, invalid, ns);
  }
  else if (is_code_expression2t(code))
  {
    // can be ignored, we don't expect sideeffects here
  }
  else if (is_code_free2t(code))
  {
    // this may kill a valid bit
    const code_free2t &ref = to_code_free2t(code);
    do_free(ref.operand, ns);
  }
  else if (is_code_printf2t(code))
  {
    // doesn't do anything
  }
  else if (is_code_return2t(code))
  {
    // this is turned into an assignment
    const code_return2t &ref = to_code_return2t(code);
    if (!is_nil_expr(ref.operand))
    {
      symbol2tc sym(ref.operand->type, "value_set::return_value");
      assign(sym, ref.operand, ns);
    }
  }
  else if (is_code_asm2t(code))
  {
    // Ignore assembly. No idea why it isn't preprocessed out anyway.
  }
  else if (is_code_cpp_delete2t(code) || is_code_cpp_del_array2t(code))
  {
    // Ignore these too
  }
  else
  {
    std::cerr << code->pretty() << std::endl;
    std::cerr << "value_sett: unexpected statement" << std::endl;
    abort();
  }
}

expr2tc value_sett::make_member(
  const expr2tc &src,
  const irep_idt &component_name,
  const namespacet &ns)
{
  const type2tc &type = src->type;
  assert(is_struct_type(type) || is_union_type(type));

  // Work around for the current lack of type inheretance
  const std::vector<type2tc> &members = (is_struct_type(type))
    ? to_struct_type(type).members : to_union_type(type).members;

  if (is_constant_struct2t(src))
  {
    unsigned no = to_struct_type(type).get_component_number(component_name);
    return to_constant_struct2t(src).datatype_members[no];
  }
  else if (is_with2t(src))
  {
    const with2t &with = to_with2t(src);
    assert(is_constant_string2t(with.update_field));
    const constant_string2t &memb_name =to_constant_string2t(with.update_field);

    if (component_name == memb_name.value)
      // yes! just take op2
      return with.update_value;
    else
      // no! do this recursively
      return make_member(with.source_value, component_name, ns);
  }
  else if (is_typecast2t(src))
  {
    // push through typecast
    return make_member(to_typecast2t(src).from, component_name, ns);
  }

  // give up
  unsigned no = static_cast<const struct_union_data&>(*type.get())
                .get_component_number(component_name);
  const type2tc &subtype = members[no];
  member2tc memb(subtype, src, component_name);
  return memb;
}

void
value_sett::dump(const namespacet &ns) const
{
  output(ns, std::cout);
}
