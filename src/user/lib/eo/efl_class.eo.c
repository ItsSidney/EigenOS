
static Eina_Bool
_efl_class_class_initializer(Efl_Class *klass)
{
   const Efl_Object_Ops *opsp = NULL;

   const Efl_Object_Property_Reflection_Ops *ropsp = NULL;

#ifdef EFL_CLASS_EXTRA_OPS
   EFL_OPS_DEFINE(ops, EFL_CLASS_EXTRA_OPS);
   opsp = &ops;
#endif

   return efl_class_functions_set(klass, opsp, ropsp);
}

static const Efl_Class_Description _efl_class_class_desc = {
   EO_VERSION,
   "Efl.Class",
   EFL_CLASS_TYPE_REGULAR_NO_INSTANT,
   0,
   _efl_class_class_initializer,
   NULL,
   NULL
};

EFL_DEFINE_CLASS(efl_class_class_get, &_efl_class_class_desc, NULL, NULL);
