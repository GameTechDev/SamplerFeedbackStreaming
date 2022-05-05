#pragma once
#include <dstorage.h>

#include "FileStreamer.h"

//=======================================================================================
//=======================================================================================
namespace Streaming
{
    class FileStreamerDS : public FileStreamer
    {
    public:
        FileStreamerDS(ID3D12Device* in_pDevice, IDStorageFactory* in_pFactory);
        virtual ~FileStreamerDS();

        virtual FileHandle* OpenFile(const std::wstring& in_path) override;
        virtual void StreamTexture(Streaming::UpdateList& in_updateList) override;

        static IDStorageFile* GetFileHandle(const FileHandle* in_pHandle);

        virtual bool GetCompleted(const Streaming::UpdateList& in_updateList) const override;

        // for DS, we don't have a way to batch batches
        // this allows the calling thread to periodically request Submit() vs. every enqueue
        virtual void Signal() override;

    private:
        class FileHandleDS : public FileHandle
        {
        public:
            FileHandleDS(IDStorageFactory* in_pFactory, const std::wstring& in_path);
            virtual ~FileHandleDS() { m_file->Close(); }

            IDStorageFile* GetHandle() const { return m_file.Get(); }
        private:
            ComPtr<IDStorageFile> m_file;
        };
        IDStorageFactory* m_pFactory{ nullptr };

        ComPtr<IDStorageQueue> m_fileQueue;

        // memory queue when for visualization modes, which copy from cpu memory
        ComPtr<IDStorageQueue> m_memoryQueue;
    };
};
