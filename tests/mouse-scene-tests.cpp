// SPDX-License-Identifier: GPL-2.0-or-later
#include "program-mouse.hpp"
#include <QApplication>
#include <obs-module.h>
#include <graphics/vec2.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace vban;
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static const char *name(void*){return "Mouse test picture";}
static void *create(obs_data_t*,obs_source_t*s){return s;}
static void destroy(void*){}
static uint32_t width(void*){return 320;}
static uint32_t height(void*){return 180;}
int main(int argc,char**argv){try{
    qputenv("QT_QPA_PLATFORM","minimal");QApplication app(argc,argv);
    check(obs_startup("en-US",nullptr,nullptr),"OBS starts");
    auto *private_data = obs_get_private_data(); obs_data_set_bool(private_data,"AbsoluteCoordinates",true); obs_data_release(private_data);
    obs_source_info source{};source.id="vban_video_input";source.type=OBS_SOURCE_TYPE_INPUT;source.output_flags=OBS_SOURCE_VIDEO;
    source.get_name=name;source.create=create;source.destroy=destroy;source.get_width=width;source.get_height=height;obs_register_source(&source);
    source.id="opaque_overlay";obs_register_source(&source);
    auto *settings=obs_data_create();obs_data_set_int(settings,"slot",0);
    auto *video=obs_source_create("vban_video_input","Video",settings,nullptr);
    auto *overlay=obs_source_create("opaque_overlay","Overlay",nullptr,nullptr);
    auto *scene=obs_scene_create("Program test");auto *item=obs_scene_add(scene,video);
    vec2 pos{50,30},scale{.5f,.5f};obs_sceneitem_set_pos(item,&pos);obs_sceneitem_set_scale(item,&scale);
    obs_sceneitem_force_update_transform(item);
    auto *root=obs_scene_get_source(scene);
    auto hit=mouse_hit(root,90,60);std::cout<<"Mapping: "<<hit.slot<<" "<<hit.x<<" "<<hit.y<<" dimensions "<<hit.width<<" "<<hit.height<<"\n";check(hit.slot==0&&hit.x==80&&hit.y==60,"Inverse scene scaling and position");
    check(mouse_hit(root,49,60).slot<0,"Outside source ignored");
    obs_sceneitem_crop crop{20,10,40,20};obs_sceneitem_set_crop(item,&crop);obs_sceneitem_force_update_transform(item);
    hit=mouse_hit(root,90,60);check(hit.x==100&&hit.y==70,"Scene crop returns ORIGINAL image coordinates");
    obs_sceneitem_set_rot(item,90);obs_sceneitem_force_update_transform(item);
    hit=mouse_hit(root,20,70);check(std::abs(hit.x-100)<=1&&std::abs(hit.y-70)<=1,"Rotated image maps correctly");
    obs_sceneitem_set_rot(item,0);obs_sceneitem_force_update_transform(item);
    auto *cover=obs_scene_add(scene,overlay);check(mouse_hit(root,90,60).slot<0,"Foreground source blocks click-through");
    obs_sceneitem_set_visible(cover,false);check(mouse_hit(root,90,60).slot==0,"Hidden overlay does not block");
    obs_sceneitem_set_visible(item,false);check(mouse_hit(root,90,60).slot<0,"Hidden video cannot receive clicks");
    obs_sceneitem_set_visible(item,true);
    obs_sceneitem_set_bounds_type(item,OBS_BOUNDS_SCALE_OUTER);
    obs_sceneitem_set_bounds_crop(item,true);check(mouse_hit(root,90,60).slot<0,"Unsupported bounds crop fails closed");obs_sceneitem_set_bounds_crop(item,false);obs_sceneitem_set_bounds_type(item,OBS_BOUNDS_NONE);
    QPointF point;
    check(program_coordinates({660,380},1,{330,190},320,180,point)&&point==QPointF(160,90),"Program physical-pixel margins");
    check(program_coordinates({660,380},2,{165,95},320,180,point)&&point==QPointF(160,90),"High-DPI Program coordinates");
    check(!program_coordinates({660,380},1,{5,5},320,180,point),"Program border ignored");
    check(program_coordinates({820,380},1,{410,190},320,180,point)&&point==QPointF(160,90),"Pillarboxing accounted for");
    obs_sceneitem_remove(cover);obs_sceneitem_remove(item);
    obs_scene_release(scene);obs_source_release(video);obs_source_release(overlay);obs_data_release(settings);obs_wait_for_destroy_queue();obs_wait_for_destroy_queue();obs_shutdown();
    std::cout<<"PASS: actual OBS scene transforms, cropping, rotation, overlay blocking and high-DPI Program coordinates.\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
