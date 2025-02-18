#include "Editor/GUI/MainGUI.hpp"
#include "Engine/Components/CSharpScriptComponent.hpp"
#include "Engine/GameObject.hpp"
#include "Engine/Mono/Format/Functions.hpp"
#include "Engine/Mono/HelperFunctions.hpp"
#include "Engine/Mono/Text3D/Functions.hpp"
#include "Engine/Mono/Time/Functions.hpp"
#include "Engine/Mono/Transform/Functions.hpp"
#include "Engine/Mono/GameObject/Functions.hpp"
#include "Engine/Mono/SpriteRenderer/Functions.hpp"
#include "Engine/Mono/Rigidbody2D/Functions.hpp"
#include "Engine/Mono/Rigidbody3D/Functions.hpp"
#include "Engine/Mono/Camera/Functions.hpp"
#include "Engine/Mono/Audio/Functions.hpp"
#include "Engine/Mono/Mathf/Functions.hpp"
#include "Engine/Mono/Entity/Functions.hpp"
#include "Engine/Mono/ModelAnimator/Functions.hpp"
#include "Engine/Mono/PointLight/Functions.hpp"
#include "mono/metadata/assembly.h"
#include "mono/metadata/image.h"
#include "mono/metadata/loader.h"
#include "mono/metadata/object-forward.h"
#include "mono/metadata/object.h"
#include <Engine/Mono/CSharp.hpp>
#include <iostream>
#include <mono/jit/jit.h>
#include <mono/metadata/appdomain.h>
#include <fstream>
#include <filesystem>
#include <glm/ext.hpp>
#include <Engine/Input/Input.hpp>
#include <Engine/Mono/HelperFunctions.hpp>
#include <thread>
#include <Engine/Runtime.hpp>
#include <imgui/ImGuiNotify.hpp>

namespace fs = std::filesystem;

namespace Engine {
    CSharp *CSharp::instance;

    char *ReadBytes(const std::string &filepath, uint32_t *outSize) {
        std::ifstream stream(filepath, std::ios::binary | std::ios::ate);

        if (!stream) {
            // Failed to open the file
            return nullptr;
        }

        std::streampos end = stream.tellg();
        stream.seekg(0, std::ios::beg);
        uint32_t size = end - stream.tellg();

        if (size == 0) {
            // File is empty
            return nullptr;
        }

        char *buffer = new char[size];
        stream.read((char *)buffer, size);
        stream.close();

        *outSize = size;
        return buffer;
    }

    MonoAssembly *LoadCSharpAssembly(const std::string &assemblyPath) {
        uint32_t file_size = 0;
        char *file_data = ReadBytes(assemblyPath, &file_size);

        MonoImageOpenStatus status;
        MonoImage *image = mono_image_open_from_data_full(file_data, file_size, 1, &status, 0);

        if (status != MONO_IMAGE_OK) {
            const char *errorMessage = mono_image_strerror(status);
            return nullptr;
        }

        MonoAssembly *assembly = mono_assembly_load_from_full(image, assemblyPath.c_str(), &status, 0);
        mono_image_close(image);

        delete[] file_data;

        return assembly;
    }

    void LoadSubClasses(MonoAssembly *assembly) {
        MonoImage *image = mono_assembly_get_image(assembly);
        const MonoTableInfo *typeDefinitionsTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
        int32_t type_count = mono_table_info_get_rows(typeDefinitionsTable);
        MonoClass *vault_script_class = mono_class_from_name(image, "Vault", "Entity");

        for (int32_t i = 0; i < type_count; i++) {
            uint32_t cols[MONO_TYPEDEF_SIZE];
            mono_metadata_decode_row(typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE);

            const char *nameSpace = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);
            const char *name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);

            MonoClass *entity_class = mono_class_from_name(image, nameSpace, name);
            bool isSubclass = mono_class_is_subclass_of(entity_class, vault_script_class, false);

            if (entity_class == vault_script_class)
                continue;

            if (isSubclass) {
                CSharp::instance->entity_classes[std::string(std::string(nameSpace) + "." + name)] = std::pair(nameSpace, name);
            }

            printf("%s.%s\n", nameSpace, name);
        }
    }

    MonoClass *GetClassInAssembly(MonoAssembly *assembly, const char *namespaceName, const char *className) {
        MonoImage *image = mono_assembly_get_image(assembly);
        MonoClass *klass = mono_class_from_name(image, namespaceName, className);

        return klass;
    }

    void CSharp::InitRuntime() {
        if (root_domain != nullptr) return;
        root_domain = mono_jit_init((char *)runtime_name.c_str());
    }

    void CSharp::InitMono() {
        mono_set_assemblies_path("./mono/lib");
        InitRuntime();

        app_domain = mono_domain_create_appdomain((char *)appdomain_name.c_str(), nullptr);
        mono_domain_set(app_domain, true);

        if (!fs::exists("./assets/VAULT_OUT/csharp-lib.dll")) return;
        core_assembly = LoadCSharpAssembly("./assets/VAULT_OUT/csharp-lib.dll");
        core_assembly_image = GetImage(core_assembly);

        LoadSubClasses(core_assembly);

        RegisterVaultFunctions();
        // DevConsoleSetup(core_assembly);
    }

    void CSharp::DevConsoleSetup(MonoAssembly *core_assembly) {
        MonoImage *image = mono_assembly_get_image(core_assembly);
        const MonoTableInfo *typeDefinitionsTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
        int32_t type_count = mono_table_info_get_rows(typeDefinitionsTable);
        MonoClass *vault_script_class = mono_class_from_name(image, "DevConsole", "Command");

        CSharp::instance->command_classes.clear();

        for (int32_t i = 0; i < type_count; i++) {
            uint32_t cols[MONO_TYPEDEF_SIZE];
            mono_metadata_decode_row(typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE);

            const char *nameSpace = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);
            const char *name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);

            MonoClass *entity_class = mono_class_from_name(image, nameSpace, name);
            bool isSubclass = mono_class_is_subclass_of(entity_class, vault_script_class, false);

            if (entity_class == vault_script_class)
                continue;

            if (isSubclass) {
                CSharp::instance->command_classes[std::string(std::string(nameSpace) + "." + name)] = std::make_unique<CSharpClass>(image, std::string(nameSpace), std::string(name));

                void *__args[] = {};

                MonoObject *exception = nullptr;
                mono_runtime_invoke(CSharp::instance->command_classes[std::string(std::string(nameSpace) + "." + name)]->GetMethod("Register", 0), CSharp::instance->command_classes[std::string(std::string(nameSpace) + "." + name)]->GetHandleTarget(), __args, &exception);

                if (exception) {
                    MonoObject *exc = NULL;
                    MonoString *str = mono_object_to_string(exception, &exc);
                    if (exc) {
                        mono_print_unhandled_exception(exc);
                    } else {
                        Editor::GUI::LogError(mono_string_to_utf8(str)); // Log log(mono_string_to_utf8(str), LOG_ERROR);
                    }
                }
                // mono_runtime_object_init(CSharp::instance->command_classes[std::string(std::string(nameSpace) + "." + name)]->GetHandleTarget());

                printf("Command: %s.%s\n", nameSpace, name);
            }
        }

        CSharp::instance->command_classes["DevConsole.CommandRegistry"] = std::make_unique<CSharpClass>(image, "DevConsole", "CommandRegistry");
    }

    void CSharp::ReloadAssembly() {
        // if (!fs::exists("assets/VAULT_OUT/cs-assembly.dll"))
        //     return;

        mono_domain_set(mono_get_root_domain(), false);
        mono_domain_unload(app_domain);

        InitMono();

        if (!Runtime::instance->isRunning) return;

        auto view = Scene::Main->EntityRegistry.view<Components::CSharpScriptComponent>();

        for (auto e : view) {
            auto &manager = Scene::Main->EntityRegistry.get<Components::CSharpScriptComponent>(e);
            manager.script_instances.clear();
            manager.Init();
            manager.OnStart();
        }
    }

    void CSharp_EditorConsole_Log(MonoString *str) {
        const std::string content = CSharpHelper::MonoStrToString(str);
        Editor::GUI::LogInfo(content);
    }

    void CSharp_EditorConsole_LogError(MonoString *str) {
        const std::string content = CSharpHelper::MonoStrToString(str);
        Editor::GUI::LogError(content);
    }

    void CSharp_EditorConsole_LogWarning(MonoString *str) {
        const std::string content = CSharpHelper::MonoStrToString(str);
        Editor::GUI::LogWarning(content);
    }

    void CSharp::RegisterVaultFunctions() {
        using namespace CSharpInternalFunctions;

        // Entity
        VAULT_REGISTER_FUNCTION_NAME("Vault.Entity::cpp_GetClassInstance", Entity_GetClassInstance);

        // GameObject
        VAULT_REGISTER_FUNCTION(GameObject_GetName);
        VAULT_REGISTER_FUNCTION(GameObject_GetTag);
        VAULT_REGISTER_FUNCTION(GameObject_GetIDByName);
        VAULT_REGISTER_FUNCTION(GameObject_GetIDByTag);
        VAULT_REGISTER_FUNCTION(Scene_LoadScene);
        VAULT_REGISTER_FUNCTION(GameObject_InstantiatePrefab);
        VAULT_REGISTER_FUNCTION(GameObject_InstantiatePrefabWithProps);

        // Transform
        VAULT_REGISTER_FUNCTION(Transform_GetPosition);
        VAULT_REGISTER_FUNCTION(Transform_GetRotation);
        VAULT_REGISTER_FUNCTION(Transform_GetScale);
        VAULT_REGISTER_FUNCTION(Transform_SetField);

        // Format
        VAULT_REGISTER_FUNCTION(float_ToString);
        VAULT_REGISTER_FUNCTION(double_ToString);
        VAULT_REGISTER_FUNCTION(int_ToString);

        // Time
        VAULT_REGISTER_FUNCTION(Time_GetDeltaTime);

        // Audio
        VAULT_REGISTER_FUNCTION(Audio2D_PlayMusic);
        VAULT_REGISTER_FUNCTION(Audio2D_StopMusic);
        VAULT_REGISTER_FUNCTION(Audio2D_PlaySound);
        VAULT_REGISTER_FUNCTION(Audio2D_StopSound);

        // Input
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::IsKeyPressed", Input::IsKeyPressed);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::IsKeyReleased", Input::IsKeyReleased);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::IsKeyDown", Input::IsKeyDown);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::IsKeyUp", Input::IsKeyUp);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::GetVerticalAxis", Input::GetVerticalAxis);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::GetHorizontalAxis", Input::GetHorizontalAxis);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::GetMouseXAxis", Input::GetMouseXAxis);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Input::GetMouseYAxis", Input::GetMouseYAxis);

        // Log
        VAULT_REGISTER_FUNCTION_NAME("Vault.Debug::Log", CSharp_EditorConsole_Log);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Debug::Error", CSharp_EditorConsole_LogError);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Debug::Warning", CSharp_EditorConsole_LogWarning);

        // Text
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_GetText", Text3D_GetText);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_GetScale", Text3D_GetScale);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_GetYOffset", Text3D_GetYOffset);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_GetColor", Text3D_GetColor);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_SetText", Text3D_SetText);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_SetColor", Text3D_SetColor);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_SetScale", Text3D_SetScale);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Text3D::Text3D_SetYOffset", Text3D_SetYOffset);

        // SpriteRenderer
        VAULT_REGISTER_FUNCTION_NAME("Vault.SpriteRenderer::SpriteRenderer_GetTexture", SpriteRenderer_GetTexture);
        VAULT_REGISTER_FUNCTION_NAME("Vault.SpriteRenderer::SpriteRenderer_SetTexture", SpriteRenderer_SetTexture);
        VAULT_REGISTER_FUNCTION_NAME("Vault.SpriteRenderer::SpriteRenderer_GetColor", SpriteRenderer_GetColor);
        VAULT_REGISTER_FUNCTION_NAME("Vault.SpriteRenderer::SpriteRenderer_SetColor", SpriteRenderer_SetColor);

        // Rigidbody2D
        VAULT_REGISTER_FUNCTION_NAME("Vault.Rigidbody2D::Rigidbody2D_GetKey", Rigidbody2D_GetKey);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody2D::", Rigidbody2D_SetVelocity);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody2D::", Rigidbody2D_SetAngularVelocity);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody2D::", Rigidbody2D_SetPosition);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody2D::", Rigidbody2D_Force);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody2D::", Rigidbody2D_Torque);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody2D::", Rigidbody2D_SetType);

        // Rigidbody3D
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_GetKey);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_SetKey);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_AddForce);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_AddTorque);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_AddForceAtPosition);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_SetVelocity);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Rigidbody3D::", Rigidbody3D_SetAngularVelocity);

        // ModelAnimator
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.ModelAnimator::", ModelAnimator_GetCurrentAnimation);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.ModelAnimator::", ModelAnimator_SetCurrentAnimation);

        // Camera
        VAULT_REGISTER_FUNCTION_NAME("Vault.Camera::Camera_GetKey", Camera_GetKey);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Camera::Camera_SetKey", Camera_SetKey);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Camera::Camera_GetFront", Camera_GetFront);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Camera::Camera_GetBool", Camera_GetBool);
        VAULT_REGISTER_FUNCTION_NAME("Vault.Camera::Camera_SetBool", Camera_SetBool);

        // Point Light
        VAULT_REGISTER_FUNCTION_NAME("Vault.PointLight::PointLight_GetKey", PointLight_GetKey);
        VAULT_REGISTER_FUNCTION_NAME("Vault.PointLight::PointLight_SetKey", PointLight_SetKey);

        // Mathf
        using namespace Mathf;
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Deg2Rad);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Rad2Deg);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Abs);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Acos);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Asin);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Atan);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Atan2);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Ceil);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Clamp);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Cos);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Sin);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Sqrt);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Tan);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Round);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Pow);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Log);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Log10);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Max);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Min);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Exp);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", Lerp);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", RandomRange);
        VAULT_REGISTER_FUNCTION_PREFIX("Vault.Mathf::", FloatRandomRange);
    }

    void CSharp::RegisterFunction(const std::string &cs_path, void *func) {
        mono_add_internal_call(cs_path.c_str(), func);
    }

    CSharp::CSharp(const std::string &lib_path, const std::string &runtime_name, const std::string &appdomain_name) : runtime_name(runtime_name), appdomain_name(appdomain_name) {
        instance = this;
        InitMono();
    }

    CSharp::~CSharp() {
    }

    MonoImage *CSharp::GetImage(MonoAssembly *core_assembly) {
        return mono_assembly_get_image(core_assembly);
    }

    void CSharp::CompileAssemblies() {
        Editor::GUI::logs.clear();
        new std::thread([&] {
            std::array<char, 1000> buffer;
            std::string result;
            std::string build_command = "cd assets";
            build_command += " && \"";
            build_command += "dotnet"; // custom dotnet path here in the future maybe??
            build_command += "\" build --property WarningLevel=0 -o VAULT_OUT";
            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(build_command.c_str(), "r"), pclose);

            if (!pipe) {
                throw std::runtime_error("popen() failed!");
            }

            while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                const std::string output = buffer.data();

                if (output.find("error") != std::string::npos) {
                    Editor::GUI::LogError(output);
                } else if (output.find("warning") != std::string::npos) {
                    Editor::GUI::LogWarning(output);
                } else if (output.find("no warning") != std::string::npos) {
                    Editor::GUI::LogInfo(output);
                } else {
                    Editor::GUI::LogInfo(output);
                }

                if (output.find("Build succeeded.") != std::string::npos) {
                    Editor::GUI::LogTick(output);
                    Editor::GUI::LogTick("Please reload assembly!");
                }
            }

            Engine::Runtime::instance->main_thread_calls.push_back([]() {
                Engine::CSharp::instance->ReloadAssembly();
                ImGui::InsertNotification({ImGuiToastType::Success, 3000, "C# Assembly compiled & reloaded successfully."});
            });
        });
    }

    CSharpClass::CSharpClass(MonoImage *image, const std::string &name_space, const std::string &name) {
        klass = mono_class_from_name(image, name_space.c_str(), name.c_str());
        instance = mono_object_new(CSharp::instance->app_domain, klass);
        gc_handle = mono_gchandle_new(instance, false);
    }

    MonoMethod *CSharpClass::GetMethod(const std::string &name, int param_count) {
        return mono_class_get_method_from_name(klass, name.c_str(), param_count);
    }

    void *CSharpClass::GetThunkFromMethod(MonoMethod *method) {
        return mono_method_get_unmanaged_thunk(method);
    }

    void *CSharpClass::GetMethodThunk(const std::string &name, int param_count) {
        return mono_method_get_unmanaged_thunk(mono_class_get_method_from_name(klass, name.c_str(), param_count));
    }

    MonoObject *CSharpClass::GetHandleTarget() {
        return mono_gchandle_get_target(gc_handle);
    }

    ScriptClass::ScriptClass(MonoImage *image, const std::string &name_space, const std::string &name) : CSharpClass(image, name_space, name) {
        update_method = GetMethod("OnUpdate", 0);
        update_thunk = (OnUpdateType)GetThunkFromMethod(update_method);

        start_method = GetMethod("OnStart", 1);
        init_method = GetMethod("OnInit", 1);
        start_thunk = (OnStartType)GetThunkFromMethod(start_method);

        OnMouseEnter_method = GetMethod("OnMouseEnter", 0);
        OnMouseExit_method = GetMethod("OnMouseExit", 0);
    }

    void ScriptClass::InitInstance(const std::string &gameObject_ID) {
        if (!inited) {
            mono_runtime_object_init(GetHandleTarget());
            inited = true;
        }

        MonoObject *exception = nullptr;
        void *p = mono_string_new(CSharp::instance->app_domain, gameObject_ID.c_str());
        mono_runtime_invoke(init_method, GetHandleTarget(), &p, &exception);

        if (exception) {
            MonoObject *exc = NULL;
            MonoString *str = mono_object_to_string(exception, &exc);
            if (exc) {
                mono_print_unhandled_exception(exc);
            } else {
                Editor::GUI::LogError(mono_string_to_utf8(str)); // Log log(mono_string_to_utf8(str), LOG_ERROR);
            }
        }
    }

    void ScriptClass::OnStart(const std::string &gameObject_ID) {
        if (!inited) {
            mono_runtime_object_init(GetHandleTarget());
            inited = true;
        }

        MonoObject *exception = nullptr;
        void *p = mono_string_new(CSharp::instance->app_domain, gameObject_ID.c_str());
        mono_runtime_invoke(start_method, GetHandleTarget(), &p, &exception);

        if (exception) {
            MonoObject *exc = NULL;
            MonoString *str = mono_object_to_string(exception, &exc);
            if (exc) {
                mono_print_unhandled_exception(exc);
            } else {
                Editor::GUI::LogError(mono_string_to_utf8(str)); // Log log(mono_string_to_utf8(str), LOG_ERROR);
            }
        }
    }

    void ScriptClass::OnUpdate() {
        MonoObject *exception = nullptr;
        update_thunk(GetHandleTarget(), &exception);

        if (exception) {
            MonoObject *exc = NULL;
            MonoString *str = mono_object_to_string(exception, &exc);
            if (exc) {
                mono_print_unhandled_exception(exc);
            } else {
                Editor::GUI::LogError(mono_string_to_utf8(str)); // Log log(mono_string_to_utf8(str), LOG_ERROR);
            }
        }
    }

    void ScriptClass::OnMouseEnter() {
        MonoObject *exception = nullptr;
        if (!OnMouseEnter_method) return;
        mono_runtime_invoke(OnMouseEnter_method, GetHandleTarget(), nullptr, &exception);

        if (exception) {
            MonoObject *exc = NULL;
            MonoString *str = mono_object_to_string(exception, &exc);
            if (exc) {
                mono_print_unhandled_exception(exc);
            } else {
                Editor::GUI::LogError(mono_string_to_utf8(str)); // Log log(mono_string_to_utf8(str), LOG_ERROR);
            }
        }
    }

    void ScriptClass::OnMouseExit() {
        MonoObject *exception = nullptr;
        if (!OnMouseExit_method) return;
        mono_runtime_invoke(OnMouseExit_method, GetHandleTarget(), nullptr, &exception);

        if (exception) {
            MonoObject *exc = NULL;
            MonoString *str = mono_object_to_string(exception, &exc);
            if (exc) {
                mono_print_unhandled_exception(exc);
            } else {
                Editor::GUI::LogError(mono_string_to_utf8(str)); // Log log(mono_string_to_utf8(str), LOG_ERROR);
            }
        }
    }
} // namespace Engine