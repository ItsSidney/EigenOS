EAPI EAPI_WEAK const Efl_Event_Description _EFL_EVENT_DEL =
   EFL_EVENT_DESCRIPTION_HOT("del");
EAPI EAPI_WEAK const Efl_Event_Description _EFL_EVENT_INVALIDATE =
   EFL_EVENT_DESCRIPTION_HOT("invalidate");
EAPI EAPI_WEAK const Efl_Event_Description _EFL_EVENT_NOREF =
   EFL_EVENT_DESCRIPTION_HOT("noref");
EAPI EAPI_WEAK const Efl_Event_Description _EFL_EVENT_OWNERSHIP_UNIQUE =
   EFL_EVENT_DESCRIPTION_HOT("ownership,unique");
EAPI EAPI_WEAK const Efl_Event_Description _EFL_EVENT_OWNERSHIP_SHARED =
   EFL_EVENT_DESCRIPTION_HOT("ownership,shared");
EAPI EAPI_WEAK const Efl_Event_Description _EFL_EVENT_DESTRUCT =
   EFL_EVENT_DESCRIPTION_HOT("destruct");

void _efl_object_parent_set(Eo *obj, Efl_Object_Data *pd, Efl_Object *parent);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_parent_set, EFL_FUNC_CALL(parent), Efl_Object *parent);

Efl_Object *_efl_object_parent_get(const Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_parent_get, Efl_Object *, NULL);

void _efl_object_name_set(Eo *obj, Efl_Object_Data *pd, const char *name);


static Eina_Error
__eolian_efl_object_name_set_reflect(Eo *obj, Eina_Value val)
{
   Eina_Error r = 0;   const char *cval;
   if (!eina_value_string_convert(&val, &cval))
      {
         r = EINA_ERROR_VALUE_FAILED;
         goto end;
      }
   efl_name_set(obj, cval);
 end:
   eina_value_flush(&val);
   return r;
}

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_name_set, EFL_FUNC_CALL(name), const char *name);

const char *_efl_object_name_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_name_get_reflect(const Eo *obj)
{
   const char *val = efl_name_get(obj);
   return eina_value_string_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_name_get, const char *, NULL);

void _efl_object_comment_set(Eo *obj, Efl_Object_Data *pd, const char *comment);


static Eina_Error
__eolian_efl_object_comment_set_reflect(Eo *obj, Eina_Value val)
{
   Eina_Error r = 0;   const char *cval;
   if (!eina_value_string_convert(&val, &cval))
      {
         r = EINA_ERROR_VALUE_FAILED;
         goto end;
      }
   efl_comment_set(obj, cval);
 end:
   eina_value_flush(&val);
   return r;
}

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_comment_set, EFL_FUNC_CALL(comment), const char *comment);

const char *_efl_object_comment_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_comment_get_reflect(const Eo *obj)
{
   const char *val = efl_comment_get(obj);
   return eina_value_string_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_comment_get, const char *, NULL);

void _efl_object_debug_name_override(Eo *obj, Efl_Object_Data *pd, Eina_Strbuf *sb);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_debug_name_override, EFL_FUNC_CALL(sb), Eina_Strbuf *sb);

int _efl_object_event_global_freeze_count_get(void);

EAPI EAPI_WEAK int efl_event_global_freeze_count_get(void)
{
   const Efl_Class *klass = efl_object_class_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(klass,0);
   return _efl_object_event_global_freeze_count_get();
}

int _efl_object_event_freeze_count_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_event_freeze_count_get_reflect(const Eo *obj)
{
   int val = efl_event_freeze_count_get(obj);
   return eina_value_int_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_event_freeze_count_get, int, 0);

Eina_Bool _efl_object_finalized_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_finalized_get_reflect(const Eo *obj)
{
   Eina_Bool val = efl_finalized_get(obj);
   return eina_value_bool_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_finalized_get, Eina_Bool, 0);

Eina_Bool _efl_object_invalidated_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_invalidated_get_reflect(const Eo *obj)
{
   Eina_Bool val = efl_invalidated_get(obj);
   return eina_value_bool_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_invalidated_get, Eina_Bool, 0);

Eina_Bool _efl_object_invalidating_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_invalidating_get_reflect(const Eo *obj)
{
   Eina_Bool val = efl_invalidating_get(obj);
   return eina_value_bool_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_invalidating_get, Eina_Bool, 0);

Efl_Object *_efl_object_provider_find(const Eo *obj, Efl_Object_Data *pd, const Efl_Class *klass);

EAPI EAPI_WEAK EFL_FUNC_BODYV_CONST(efl_provider_find, Efl_Object *, NULL, EFL_FUNC_CALL(klass), const Efl_Class *klass);

Efl_Object *_efl_object_constructor(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_FUNC_BODY(efl_constructor, Efl_Object *, NULL);

void _efl_object_destructor(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODY(efl_destructor);

Efl_Object *_efl_object_finalize(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_FUNC_BODY(efl_finalize, Efl_Object *, NULL);

void _efl_object_invalidate(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODY(efl_invalidate);

Efl_Object *_efl_object_name_find(const Eo *obj, Efl_Object_Data *pd, const char *search);

EAPI EAPI_WEAK EFL_FUNC_BODYV_CONST(efl_name_find, Efl_Object *, NULL, EFL_FUNC_CALL(search), const char *search);

void _efl_object_event_thaw(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODY(efl_event_thaw);

void _efl_object_event_freeze(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODY(efl_event_freeze);

void _efl_object_event_global_thaw(void);

EAPI EAPI_WEAK void efl_event_global_thaw(void)
{
   const Efl_Class *klass = efl_object_class_get();
   EINA_SAFETY_ON_NULL_RETURN(klass);
   _efl_object_event_global_thaw();
}

void _efl_object_event_global_freeze(void);

EAPI EAPI_WEAK void efl_event_global_freeze(void)
{
   const Efl_Class *klass = efl_object_class_get();
   EINA_SAFETY_ON_NULL_RETURN(klass);
   _efl_object_event_global_freeze();
}

void _efl_object_event_callback_stop(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODY(efl_event_callback_stop);

void _efl_object_event_callback_forwarder_priority_add(Eo *obj, Efl_Object_Data *pd, const Efl_Event_Description *desc, Efl_Callback_Priority priority, Efl_Object *source);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_event_callback_forwarder_priority_add, EFL_FUNC_CALL(desc, priority, source), const Efl_Event_Description *desc, Efl_Callback_Priority priority, Efl_Object *source);

void _efl_object_event_callback_forwarder_del(Eo *obj, Efl_Object_Data *pd, const Efl_Event_Description *desc, Efl_Object *new_obj);

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_event_callback_forwarder_del, EFL_FUNC_CALL(desc, new_obj), const Efl_Event_Description *desc, Efl_Object *new_obj);

Eina_Iterator *_efl_object_children_iterator_new(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_FUNC_BODY(efl_children_iterator_new, Eina_Iterator *, NULL);

Eina_Bool _efl_object_composite_attach(Eo *obj, Efl_Object_Data *pd, Efl_Object *comp_obj);

EAPI EAPI_WEAK EFL_FUNC_BODYV(efl_composite_attach, Eina_Bool, 0, EFL_FUNC_CALL(comp_obj), Efl_Object *comp_obj);

Eina_Bool _efl_object_composite_detach(Eo *obj, Efl_Object_Data *pd, Efl_Object *comp_obj);

EAPI EAPI_WEAK EFL_FUNC_BODYV(efl_composite_detach, Eina_Bool, 0, EFL_FUNC_CALL(comp_obj), Efl_Object *comp_obj);

Eina_Bool _efl_object_composite_part_is(Eo *obj, Efl_Object_Data *pd);

EAPI EAPI_WEAK EFL_FUNC_BODY(efl_composite_part_is, Eina_Bool, 0);

void _efl_object_allow_parent_unref_set(Eo *obj, Efl_Object_Data *pd, Eina_Bool allow);


static Eina_Error
__eolian_efl_object_allow_parent_unref_set_reflect(Eo *obj, Eina_Value val)
{
   Eina_Error r = 0;   Eina_Bool cval;
   if (!eina_value_bool_convert(&val, &cval))
      {
         r = EINA_ERROR_VALUE_FAILED;
         goto end;
      }
   efl_allow_parent_unref_set(obj, cval);
 end:
   eina_value_flush(&val);
   return r;
}

EAPI EAPI_WEAK EFL_VOID_FUNC_BODYV(efl_allow_parent_unref_set, EFL_FUNC_CALL(allow), Eina_Bool allow);

Eina_Bool _efl_object_allow_parent_unref_get(const Eo *obj, Efl_Object_Data *pd);


static Eina_Value
__eolian_efl_object_allow_parent_unref_get_reflect(const Eo *obj)
{
   Eina_Bool val = efl_allow_parent_unref_get(obj);
   return eina_value_bool_init(val);
}

EAPI EAPI_WEAK EFL_FUNC_BODY_CONST(efl_allow_parent_unref_get, Eina_Bool, EINA_FALSE /* false */);

Eina_Bool _efl_object_provider_register(Eo *obj, Efl_Object_Data *pd, const Efl_Class *klass, const Efl_Object *provider);

EAPI EAPI_WEAK EFL_FUNC_BODYV(efl_provider_register, Eina_Bool, 0, EFL_FUNC_CALL(klass, provider), const Efl_Class *klass, const Efl_Object *provider);

Eina_Bool _efl_object_provider_unregister(Eo *obj, Efl_Object_Data *pd, const Efl_Class *klass, const Efl_Object *provider);

EAPI EAPI_WEAK EFL_FUNC_BODYV(efl_provider_unregister, Eina_Bool, 0, EFL_FUNC_CALL(klass, provider), const Efl_Class *klass, const Efl_Object *provider);

static Eina_Bool
_efl_object_class_initializer(Efl_Class *klass)
{
   const Efl_Object_Ops *opsp = NULL;

   const Efl_Object_Property_Reflection_Ops *ropsp = NULL;

#ifndef EFL_OBJECT_EXTRA_OPS
#define EFL_OBJECT_EXTRA_OPS
#endif

   EFL_OPS_DEFINE(ops,
      EFL_OBJECT_OP_FUNC(efl_parent_set, _efl_object_parent_set),
      EFL_OBJECT_OP_FUNC(efl_parent_get, _efl_object_parent_get),
      EFL_OBJECT_OP_FUNC(efl_name_set, _efl_object_name_set),
      EFL_OBJECT_OP_FUNC(efl_name_get, _efl_object_name_get),
      EFL_OBJECT_OP_FUNC(efl_comment_set, _efl_object_comment_set),
      EFL_OBJECT_OP_FUNC(efl_comment_get, _efl_object_comment_get),
      EFL_OBJECT_OP_FUNC(efl_debug_name_override, _efl_object_debug_name_override),
      EFL_OBJECT_OP_FUNC(efl_event_freeze_count_get, _efl_object_event_freeze_count_get),
      EFL_OBJECT_OP_FUNC(efl_finalized_get, _efl_object_finalized_get),
      EFL_OBJECT_OP_FUNC(efl_invalidated_get, _efl_object_invalidated_get),
      EFL_OBJECT_OP_FUNC(efl_invalidating_get, _efl_object_invalidating_get),
      EFL_OBJECT_OP_FUNC(efl_provider_find, _efl_object_provider_find),
      EFL_OBJECT_OP_FUNC(efl_constructor, _efl_object_constructor),
      EFL_OBJECT_OP_FUNC(efl_destructor, _efl_object_destructor),
      EFL_OBJECT_OP_FUNC(efl_finalize, _efl_object_finalize),
      EFL_OBJECT_OP_FUNC(efl_invalidate, _efl_object_invalidate),
      EFL_OBJECT_OP_FUNC(efl_name_find, _efl_object_name_find),
      EFL_OBJECT_OP_FUNC(efl_event_thaw, _efl_object_event_thaw),
      EFL_OBJECT_OP_FUNC(efl_event_freeze, _efl_object_event_freeze),
      EFL_OBJECT_OP_FUNC(efl_event_callback_stop, _efl_object_event_callback_stop),
      EFL_OBJECT_OP_FUNC(efl_event_callback_forwarder_priority_add, _efl_object_event_callback_forwarder_priority_add),
      EFL_OBJECT_OP_FUNC(efl_event_callback_forwarder_del, _efl_object_event_callback_forwarder_del),
      EFL_OBJECT_OP_FUNC(efl_children_iterator_new, _efl_object_children_iterator_new),
      EFL_OBJECT_OP_FUNC(efl_composite_attach, _efl_object_composite_attach),
      EFL_OBJECT_OP_FUNC(efl_composite_detach, _efl_object_composite_detach),
      EFL_OBJECT_OP_FUNC(efl_composite_part_is, _efl_object_composite_part_is),
      EFL_OBJECT_OP_FUNC(efl_allow_parent_unref_set, _efl_object_allow_parent_unref_set),
      EFL_OBJECT_OP_FUNC(efl_allow_parent_unref_get, _efl_object_allow_parent_unref_get),
      EFL_OBJECT_OP_FUNC(efl_provider_register, _efl_object_provider_register),
      EFL_OBJECT_OP_FUNC(efl_provider_unregister, _efl_object_provider_unregister),
      EFL_OBJECT_EXTRA_OPS
   );
   opsp = &ops;

   static const Efl_Object_Property_Reflection refl_table[] = {
      {"name", __eolian_efl_object_name_set_reflect, __eolian_efl_object_name_get_reflect},
      {"comment", __eolian_efl_object_comment_set_reflect, __eolian_efl_object_comment_get_reflect},
      {"event_freeze_count", NULL, __eolian_efl_object_event_freeze_count_get_reflect},
      {"finalized", NULL, __eolian_efl_object_finalized_get_reflect},
      {"invalidated", NULL, __eolian_efl_object_invalidated_get_reflect},
      {"invalidating", NULL, __eolian_efl_object_invalidating_get_reflect},
      {"allow_parent_unref", __eolian_efl_object_allow_parent_unref_set_reflect, __eolian_efl_object_allow_parent_unref_get_reflect},
   };
   static const Efl_Object_Property_Reflection_Ops rops = {
      refl_table, EINA_C_ARRAY_LENGTH(refl_table)
   };
   ropsp = &rops;

   return efl_class_functions_set(klass, opsp, ropsp);
}

static const Efl_Class_Description _efl_object_class_desc = {
   EO_VERSION,
   "Efl.Object",
   EFL_CLASS_TYPE_REGULAR_NO_INSTANT,
   sizeof(Efl_Object_Data),
   _efl_object_class_initializer,
   _efl_object_class_constructor,
   _efl_object_class_destructor
};

EFL_DEFINE_CLASS(efl_object_class_get, &_efl_object_class_desc, NULL, NULL);
