#include <cpp/cpp_typecheck.h>
#include <util/expr_util.h>
#include <util/std_expr.h>
#include <util/std_types.h>

void cpp_typecheckt::do_virtual_table(const symbolt &symbol)
{
  // builds virtual-table value maps: (class x virtual_name x value)
  std::map<irep_idt, std::map<irep_idt, exprt>> vt_value_maps;

  const struct_typet &struct_type = to_struct_type(symbol.type);
  for(const auto &compo : struct_type.components())
  {
    if(!compo.get_bool("is_virtual"))
      continue;

    const code_typet &code_type = to_code_type(compo.type());
    assert(code_type.arguments().size() > 0);

    const pointer_typet &pointer_type =
      static_cast<const pointer_typet &>(code_type.arguments()[0].type());

    irep_idt class_id = pointer_type.subtype().identifier();

    std::map<irep_idt, exprt> &value_map = vt_value_maps[class_id];

    exprt e = symbol_exprt(compo.get_name(), code_type);

    if(compo.get_bool("is_pure_virtual"))
    {
      pointer_typet pointer_type(code_type);
      e = gen_zero(pointer_type);
      assert(e.is_not_nil());
      value_map[compo.get("virtual_name")] = e;
    }
    else
    {
      address_of_exprt address(e);
      value_map[compo.get("virtual_name")] = address;
    }
  }

  // create virtual-table symbol variables
  for(std::map<irep_idt, std::map<irep_idt, exprt>>::const_iterator cit =
        vt_value_maps.begin();
      cit != vt_value_maps.end();
      cit++)
  {
    const std::map<irep_idt, exprt> &value_map = cit->second;

    const symbolt &late_cast_symb = *namespacet(context).lookup(cit->first);
    const symbolt &vt_symb_type = *namespacet(context).lookup(
      "virtual_table::" + late_cast_symb.id.as_string());

    symbolt vt_symb_var;
    vt_symb_var.id = vt_symb_type.id.as_string() + "@" + symbol.id.as_string();
    vt_symb_var.name =
      vt_symb_type.name.as_string() + "@" + symbol.name.as_string();
    vt_symb_var.mode = current_mode;
    vt_symb_var.module = module;
    vt_symb_var.location = vt_symb_type.location;
    vt_symb_var.type = symbol_typet(vt_symb_type.id);
    vt_symb_var.lvalue = true;
    vt_symb_var.static_lifetime = true;

    // do the values
    const struct_typet &vt_type = to_struct_type(vt_symb_type.type);
    exprt values("struct", symbol_typet(vt_symb_type.id));
    for(const auto &compo : vt_type.components())
    {
      std::map<irep_idt, exprt>::const_iterator cit2 =
        value_map.find(compo.base_name());
      assert(cit2 != value_map.end());
      const exprt &value = cit2->second;
      assert(value.type() == compo.type());
      values.operands().push_back(value);
    }
    vt_symb_var.value = values;

    bool failed = context.move(vt_symb_var);
    assert(!failed);
    (void)failed; // ndebug
  }
}
