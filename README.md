# SimpleDraw

[![Windows](https://img.shields.io/badge/Platform-Windows-blue.svg)](https://github.com/)
[![Language](https://img.shields.io/badge/Language-C%2B%2B-green.svg)](https://isocpp.org/)
[![API](https://img.shields.io/badge/API-Win32-orange.svg)](https://docs.microsoft.com/en-us/windows/win32/)

一个基于 Win32 开发的轻量级绘图应用程序，支持线段和矩形的绘制、撤销、清除和保存功能。程序采用消息驱动机制和双缓冲技术实现无闪烁绘图，并使用工作线程处理耗时操作确保界面流畅。

## 📋 目录

- [功能特性](#-功能特性)
- [快速开始](#-快速开始)
- [系统架构](#-系统架构)
- [核心机制详解](#-核心机制详解)
  - [消息机制](#1-消息机制)
  - [线程模型](#2-线程模型)
- [模块设计](#-模块设计)
  - [图形管理模块](#1-图形管理模块)
  - [绘制模块](#2-绘制模块)
  - [操作撤销模块](#3-操作撤销模块)
  - [文件保存模块](#4-文件保存模块)
- [事件处理流程](#-事件处理流程)
- [内存管理](#-内存管理)
- [性能优化](#-性能优化)
- [技术要点](#-技术要点)
- [许可证](#-许可证)

## ✨ 功能特性

- **两种图形绘制**：支持线段和矩形绘制
- **实时预览**：绘制过程中显示红色虚线预览
- **右键擦除**：右键点击图形可删除
- **操作撤销**：支持撤销添加/删除操作
- **一键清除**：清空所有图形
- **文件保存**：将画板内容保存为 BMP 格式
- **无闪烁绘制**：采用双缓冲技术
- **异步保存**：工作线程处理文件保存，避免界面卡顿

## 🚀 快速开始

### 环境要求

- Windows 操作系统（Windows 7+）
- Visual Studio 2019/2022（推荐）
- C++17 兼容的编译器

### 编译运行

1. 克隆仓库
   ```bash
   git clone https://github.com/yourusername/SimpleDraw.git
