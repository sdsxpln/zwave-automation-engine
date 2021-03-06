#include "scene.h"
#include <string.h>
#include <stdlib.h>
#include "command_parser.h"
#include "scene_action.h"
#include <logger.h>

DECLARE_LOGGER(Scene)

void scene_delete(void* arg)
{
    scene_t* scene = (scene_t*)arg;
    free(scene->name);
    stack_free(scene->actions);
    stack_free(scene->source_list);
    stack_free(scene->condition_list);
    free(scene);
}

scene_t*    scene_create(const char* name)
{
    scene_t* new_scene = (scene_t*)calloc(1, sizeof(scene_t));
    new_scene->actions = stack_create();
    new_scene->is_valid = true;
    new_scene->is_enabled = true;
    new_scene->name = strdup(name);
    new_scene->source_list = stack_create();
    new_scene->condition_list = stack_create();

    return new_scene;
}

bool        scene_add_action(scene_t* scene, action_t* action)
{
    stack_for_each(scene->actions, action_variant)
    {
        action_t* scene_action = (action_t*)variant_get_ptr(action_variant);
        // We dont want to add action with the same path
        if(strcmp(scene_action->path, action->path) == 0)
        {
            scene_action_delete(action);
            return false;
        }
    }
    stack_push_back(scene->actions, variant_create_ptr(DT_PTR, action, &scene_action_delete));
    return true;
}

bool    scene_source_compare(variant_t* item, void* other)
{
    return strcmp(variant_get_string(item), (char*)other) == 0;
}

void        scene_add_source(scene_t* scene, const char* source)
{
    if(!stack_is_exists(scene->source_list, scene_source_compare, (void*)source))
    {
        stack_push_back(scene->source_list, variant_create_string(source));
    }
}

void        scene_del_source(scene_t* scene, const char* source)
{
    stack_for_each(scene->source_list, source_variant)
    {
        if(strcmp(source, variant_get_string(source_variant)) == 0)
        {
            stack_remove(scene->source_list, source_variant);
            break;
        }
    }
}

/*scene_t*    scene_load(struct json_object* scene_obj)
{
    scene_t* new_scene = NULL;

    if(NULL != scene_obj)
    {
        new_scene = (scene_t*)malloc(sizeof(scene_t));
        new_scene->actions = stack_create();
        new_scene->is_valid = true;

        struct json_object* value;
        if(json_object_object_get_ex(scene_obj, "scene", &value))
        {
            json_object_object_foreach(value, key, val)
            {
                if(strcmp(key, "name") == 0)
                {
                    new_scene->name = strdup(json_object_get_string(val));
                }
                else if(strcmp(key, "source") == 0)
                {
                    LOG_DEBUG(Scene, "Scene source: %s", json_object_get_string(val));
                    new_scene->source = strdup(json_object_get_string(val)); //resolver_nodeId_from_name(resolver, json_object_get_string(val));
                }
                else if(strcmp(key, "condition") == 0)
                {
                    LOG_DEBUG(Scene, "Scene condition: %s", json_object_get_string(val));
                    new_scene->condition = strdup(json_object_get_string(val));
                }
                else if(strcmp(key, "actions") == 0)
                {
                    int num_actions = json_object_array_length(val);

                    LOG_DEBUG(Scene, "Found %d scene actions", num_actions);
                    for(int i = 0; i < num_actions; i++)
                    {
                        struct json_object* record = json_object_array_get_idx(val, i);
                        action_t* new_action = scene_action_create_old(record);
                        stack_push_back(new_scene->actions, variant_create_ptr(DT_PTR, new_action, &scene_action_delete));
                    }
                }
            }
        }
    }

    return new_scene;
}*/

void        scene_del_action(scene_t* scene, action_t* action)
{
    stack_for_each(scene->actions, action_data)
    {
        action_t* scene_action = (action_t*)variant_get_ptr(action_data);
        if(scene_action == action)
        {
            stack_remove(scene->actions, action_data);
            variant_free(action_data);
            break;
        }
    }
}

action_t*   scene_get_action(scene_t* scene, const char* action_path)
{
    stack_for_each(scene->actions, action_data)
    {
        action_t* action = (action_t*)variant_get_ptr(action_data);
        if(strcmp(action->path, action_path) == 0)
        {
            return action;
        }
    }
    
    return NULL;
}

action_t*   scene_get_action_with_type(scene_t* scene, const char* action_path, ActionType type)
{
    stack_for_each(scene->actions, action_data)
    {
        action_t* action = (action_t*)variant_get_ptr(action_data);
        if(strcmp(action->path, action_path) == 0 && action->type == type)
        {
            return action;
        }
    }
    
    return NULL;
}

void scene_exec(scene_t* scene)
{
    if(!scene->is_valid)
    {
        LOG_ERROR(Scene, "Unable to execute: invalid scene %s", scene->name);
        return;
    }

    if(!scene->is_enabled)
    {
        LOG_ADVANCED(Scene, "Unable to execute: scene %s is disabled", scene->name);
        return;
    }
    
    bool isOk;
    bool condition_match = false;
    stack_for_each(scene->condition_list, condition_var)
    {
        variant_stack_t* compiled_condition = command_parser_compile_expression(variant_get_string(condition_var), &isOk);
    
        if(!isOk)
        {
            LOG_ERROR(Scene, "Error compiling condition %s", variant_get_string(condition_var));
        }
        else
        {
            variant_t* condition = command_parser_execute_expression(compiled_condition);
            condition_match = (NULL != condition && variant_get_bool(condition));
            stack_free(compiled_condition);
            variant_free(condition);
        }

        if(condition_match)
        {
            break;
        }
    }

    if(condition_match)
    {
        LOG_ADVANCED(Scene, "Scene %s condition match", scene->name);
        // The condition is true - execute scene triggers!
        stack_for_each(scene->actions, action_data)
        {
            action_t* action = (action_t*)variant_get_ptr(action_data);

            if(NULL != action)
            {
                LOG_DEBUG(Scene, "Call action %s", action->path);
                scene_action_exec(action);
            }
        }
    }
    else
    {
        LOG_ADVANCED(Scene, "Scene %s condition mismatch", scene->name);
    }
}

void    scene_for_each_action(scene_t* scene, void (*visitor)(action_t*, void*), void* arg)
{
    stack_for_each(scene->actions, action_data)
    {
        action_t* action = (action_t*)variant_get_ptr(action_data);
        visitor(action, arg);
    }
}

