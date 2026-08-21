/* eotest.c — minimal Eo (EFL object system) smoke test for EigenOS.
 * Verifies eina_init + eo_init succeed and the base Efl.Object class is
 * registered. Prints markers via eigen_puts so output is visible even if the
 * app crashes (printf is buffered through the same path, but eigen_puts goes
 * straight to serial). */

#include <Eina.h>
#include <Eo.h>
#include <userlib.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    eigen_puts("[EOTEST] START\n");

    if (!eina_init()) {
        eigen_puts("[EOTEST] eina_init() FAILED\n");
        return 1;
    }
    eigen_puts("[EOTEST] eina_init() OK\n");

    if (!efl_object_init()) {
        eigen_puts("[EOTEST] efl_object_init() FAILED\n");
        return 1;
    }
    eigen_puts("[EOTEST] efl_object_init() OK\n");

    const Efl_Class *klass = efl_object_class_get();
    if (!klass) {
        eigen_puts("[EOTEST] efl_object_class_get() returned NULL\n");
        return 1;
    }
    eigen_puts("[EOTEST] efl_object_class_get() OK\n");

    /* Efl.Object is declared abstract (EFL_CLASS_TYPE_REGULAR_NO_INSTANT)
     * and therefore cannot be instantiated directly. To exercise real object
     * construction/teardown, define a tiny concrete subclass and spawn it. */
    static const Efl_Class_Description test_desc = {
        EO_VERSION,
        "Eotest.Obj",
        EFL_CLASS_TYPE_REGULAR,
        sizeof(void *),
        NULL, NULL, NULL
    };
    const Efl_Class *test_class = efl_class_new(&test_desc, EFL_OBJECT_CLASS, NULL);
    if (!test_class) {
        eigen_puts("[EOTEST] efl_class_new() returned NULL\n");
        return 1;
    }
    eigen_puts("[EOTEST] efl_class_new() OK\n");

    Eo *obj = efl_add_ref(test_class, NULL);
    if (!obj) {
        eigen_puts("[EOTEST] efl_add() returned NULL\n");
        return 1;
    }
    efl_unref(obj);
    eigen_puts("[EOTEST] efl_add() OK\n");

    efl_object_shutdown();
    eina_shutdown();
    eigen_puts("[EOTEST] ALL PASS\n");
    return 0;
}
