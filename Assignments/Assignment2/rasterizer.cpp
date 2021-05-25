// clang-format off
//
// Created by goksu on 4/6/19.
//

#include <algorithm>
#include <vector>
#include "rasterizer.hpp"
#include <opencv2/opencv.hpp>
#include <math.h>
#include <iostream>

rst::pos_buf_id rst::rasterizer::load_positions(const std::vector<Eigen::Vector3f> &positions)
{
    auto id = get_next_id();
    pos_buf.emplace(id, positions);

    return {id};
}

rst::ind_buf_id rst::rasterizer::load_indices(const std::vector<Eigen::Vector3i> &indices)
{
    auto id = get_next_id();
    ind_buf.emplace(id, indices);

    return {id};
}

rst::col_buf_id rst::rasterizer::load_colors(const std::vector<Eigen::Vector3f> &cols)
{
    auto id = get_next_id();
    col_buf.emplace(id, cols);

    return {id};
}

auto to_vec4(const Eigen::Vector3f& v3, float w = 1.0f)
{
    return Vector4f(v3.x(), v3.y(), v3.z(), w);
}


static bool insideTriangle(float x, float y, const Vector3f* _v)
{   
    // TODO : Implement this function to check if the point (x, y) is inside the triangle represented by _v[0], _v[1], _v[2]

    Eigen::Vector3f p = {x, y, 0};
    Eigen::Vector3f A = {_v[0].x(),_v[0].y(),0};
    Eigen::Vector3f B = {_v[1].x(),_v[1].y(),0};
    Eigen::Vector3f C = {_v[2].x(),_v[2].y(),0};

    Eigen::Vector3f AB = B - A;
    Eigen::Vector3f AP = p - A;
    bool side_AB = (AB.cross(AP)).z() > 0;

    Eigen::Vector3f BC = C - B;
    Eigen::Vector3f BP = p - B;
    bool side_BC = (BC.cross(BP)).z() > 0;

    Eigen::Vector3f CA = A - C;
    Eigen::Vector3f CP = p - C;
    bool side_CA = (CA.cross(CP)).z() > 0;

    return (side_AB == side_BC && side_BC == side_CA);
}

static std::tuple<float, float, float> computeBarycentric2D(float x, float y, const Vector3f* v)
{
    float c1 = (x*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*y + v[1].x()*v[2].y() - v[2].x()*v[1].y()) / (v[0].x()*(v[1].y() - v[2].y()) + (v[2].x() - v[1].x())*v[0].y() + v[1].x()*v[2].y() - v[2].x()*v[1].y());
    float c2 = (x*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*y + v[2].x()*v[0].y() - v[0].x()*v[2].y()) / (v[1].x()*(v[2].y() - v[0].y()) + (v[0].x() - v[2].x())*v[1].y() + v[2].x()*v[0].y() - v[0].x()*v[2].y());
    float c3 = (x*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*y + v[0].x()*v[1].y() - v[1].x()*v[0].y()) / (v[2].x()*(v[0].y() - v[1].y()) + (v[1].x() - v[0].x())*v[2].y() + v[0].x()*v[1].y() - v[1].x()*v[0].y());
    return {c1,c2,c3};
}

void rst::rasterizer::draw(pos_buf_id pos_buffer, ind_buf_id ind_buffer, col_buf_id col_buffer, Primitive type)
{
    auto& buf = pos_buf[pos_buffer.pos_id];
    auto& ind = ind_buf[ind_buffer.ind_id];
    auto& col = col_buf[col_buffer.col_id];

    float f1 = (50 - 0.1) / 2.0;
    float f2 = (50 + 0.1) / 2.0;

    Eigen::Matrix4f mvp = projection * view * model;
    for (auto& i : ind)
    {
        Triangle t;
        Eigen::Vector4f v[] = {
                mvp * to_vec4(buf[i[0]], 1.0f),
                mvp * to_vec4(buf[i[1]], 1.0f),
                mvp * to_vec4(buf[i[2]], 1.0f)
        };
        //Homogeneous division
        for (auto& vec : v) {
            vec /= vec.w();
        }
        //Viewport transformation
        for (auto & vert : v)
        {
            vert.x() = 0.5*width*(vert.x()+1.0);
            vert.y() = 0.5*height*(vert.y()+1.0);
            vert.z() = vert.z() * f1 + f2;
        }

        for (int i = 0; i < 3; ++i)
        {
            t.setVertex(i, v[i].head<3>());
            t.setVertex(i, v[i].head<3>());
            t.setVertex(i, v[i].head<3>());
        }

        auto col_x = col[i[0]];
        auto col_y = col[i[1]];
        auto col_z = col[i[2]];

        t.setColor(0, col_x[0], col_x[1], col_x[2]);
        t.setColor(1, col_y[0], col_y[1], col_y[2]);
        t.setColor(2, col_z[0], col_z[1], col_z[2]);

        rasterize_triangle(t);
    }

    if (bSSAA)
    {
        for(int x = 0; x < width; ++x)
        {
            for(int y = 0; y < height; ++y)
            {
                int _x = 2 * x; 
                int _y = 2 * y;
                Vector3f color1 = color_buf[get_depbuf_index(_x,_y)];
                Vector3f color2 = color_buf[get_depbuf_index(_x,_y+1)];
                Vector3f color3 = color_buf[get_depbuf_index(_x+1,_y)];
                Vector3f color4 = color_buf[get_depbuf_index(_x+1,_y+1)];
                Vector3f color = (color1 + color2 + color3 + color4) / 4.0f;
                Vector3f point = {(float)x, (float)y, 0};
                set_pixel(point, color);
            }
        }
    }
}

void rst::rasterizer::SSAA(float x, float y, const Triangle& t) {
    auto v = t.toVector4();
    auto[alpha, beta, gamma] = computeBarycentric2D(x, y, t.v);
    float w_reciprocal = 1.0/(alpha / v[0].w() + beta / v[1].w() + gamma / v[2].w());
    float z_interpolated = alpha * v[0].z() / v[0].w() + beta * v[1].z() / v[1].w() + gamma * v[2].z() / v[2].w();
    z_interpolated *= w_reciprocal;

    int _x = floor(2 * x); 
    int _y = floor(2 * y);
    float z_depth = depth_buf[get_depbuf_index(_x,_y)];
    if(-z_interpolated < z_depth)
    {
        depth_buf[get_depbuf_index(_x,_y)] = -z_interpolated;
        Vector3f color = t.getColor();
        color_buf[get_depbuf_index(_x,_y)] = color;
    }
}

//Screen space rasterization
void rst::rasterizer::rasterize_triangle(const Triangle& t) {
    auto v = t.toVector4();

    // bounding box
	int min_x = (int)std::floor(std::min(v[0][0], std::min(v[1][0], v[2][0])));
    int max_x = (int)std::ceil(std::max(v[0][0], std::max(v[1][0], v[2][0])));
	int min_y = (int)std::floor(std::min(v[0][1], std::min(v[1][1], v[2][1])));
	int max_y = (int)std::ceil(std::max(v[0][1], std::max(v[1][1], v[2][1])));
    if(bSSAA)
    {
        for (int x = min_x; x <= max_x; x++) 
        {
            for (int y = min_y; y <= max_y; y++) 
            {
                //在三角形内点的数量
                if(insideTriangle(x + 0.25f,y + 0.25f,t.v))
                {
                    SSAA(x + 0.25f,y + 0.25f, t);
                }
                if(insideTriangle(x + 0.25f,y + 0.75f,t.v))
                {
                    SSAA(x + 0.25f,y + 0.75f, t);
                }
                if(insideTriangle(x + 0.75f,y + 0.25f,t.v))
                {
                    SSAA(x + 0.75f,y + 0.25f, t);
                }
                if(insideTriangle(x + 0.75f,y + 0.75f,t.v))
                {
                    SSAA(x + 0.75f,y + 0.75f, t);
                }
            }
        }
    }
    else
    {
        for (int x = min_x; x <= max_x; x++) 
        {
            for (int y = min_y; y <= max_y; y++) 
            {
                if (insideTriangle((float)x, (float)y, t.v)) 
                {
                    auto[alpha, beta, gamma] = computeBarycentric2D(x, y, t.v);
                    float w_reciprocal = 1.0/(alpha / v[0].w() + beta / v[1].w() + gamma / v[2].w());
                    float z_interpolated = alpha * v[0].z() / v[0].w() + beta * v[1].z() / v[1].w() + gamma * v[2].z() / v[2].w();
                    z_interpolated *= w_reciprocal;
                    if (-z_interpolated < depth_buf[get_depbuf_index(x, y)]) 
                    {
                        Vector3f color = t.getColor();
                        Vector3f point = {(float)x, (float)y, 0};
                        set_pixel(point, color);
                        depth_buf[get_index(x, y)] = -z_interpolated;
                    }
                }
            }
        }
    }
    
    
    
}

void rst::rasterizer::set_model(const Eigen::Matrix4f& m)
{
    model = m;
}

void rst::rasterizer::set_view(const Eigen::Matrix4f& v)
{
    view = v;
}

void rst::rasterizer::set_projection(const Eigen::Matrix4f& p)
{
    projection = p;
}

void rst::rasterizer::clear(rst::Buffers buff)
{
    if ((buff & rst::Buffers::Color) == rst::Buffers::Color)
    {
        std::fill(frame_buf.begin(), frame_buf.end(), Eigen::Vector3f{0, 0, 0});
    }
    if ((buff & rst::Buffers::Depth) == rst::Buffers::Depth)
    {
        std::fill(depth_buf.begin(), depth_buf.end(), std::numeric_limits<float>::infinity());
    }
}

rst::rasterizer::rasterizer(int w, int h, bool bSSAA) : width(w), height(h), bSSAA(bSSAA)
{
    frame_buf.resize(w * h);
    if(bSSAA)
    {
        depth_buf.resize(4 * w * h);
        color_buf.resize(4 * w * h);
    }
    else
    {
        depth_buf.resize(w * h);
    }
}

int rst::rasterizer::get_depbuf_index(int x, int y)
{
    if (bSSAA)
    {    
        return (2*height-1-y)*2*width + x;
    }
    else
    {
        return (height-1-y)*width + x;
    }
   
}

int rst::rasterizer::get_index(int x, int y)
{
    return (height-1-y)*width + x;
}

void rst::rasterizer::set_pixel(const Eigen::Vector3f& point, const Eigen::Vector3f& color)
{
    //old index: auto ind = point.y() + point.x() * width;
    auto ind = (height-1-point.y())*width + point.x();
    frame_buf[ind] = color;

}

// clang-format on