#define _CRT_SECURE_NO_WARNINGS
#include "DomRenderer.h"

using std::vector;
using std::string;
using std::list;
using std::array;
using std::map;
using std::dynamic_pointer_cast;
using std::shared_ptr;

namespace SnooDom
{
	void* rndr_allocate(void *opaque, size_t size);

	struct dom_builder_state
	{
		dom_builder_state() : domId(1) {}
		uint32_t domId;
		map<uint32_t, IDomObject*> unclaimedDomIdMap;
		SimpleSessionMemoryPool memoryPool;
	};

	string toPlatformString(const char* src, uint32_t sourceLength)
	{
		return string(src, sourceLength);
	}

	string toPlatformString(const struct buf * buffer)
	{
		if(buffer == nullptr || buffer->size == 0)
			return string();

		return toPlatformString((const char*)buffer->data, buffer->size);
	}

	void makeDomId(struct buf* ob, int domId, void* opaque)
	{
		array<uint8_t, 6> result;
		memset(&result[0], 4, 6);
		result[0] = 3;
		auto itoaLen = strlen(_itoa(domId, (char*)&result[1], 10));
		result[itoaLen + 1] = 4;
		bufput(opaque, rndr_allocate, ob, (const char*)&result[0], 6);
	}

	uint32_t nextId(const struct buf * text, ptrdiff_t& offset, std::string& plainText)
	{
		for(size_t i = offset; i < text->size;i++)
		{
			//start sentinal
			if(text->data[i] == 3)
			{
				if(i - offset > 0)
				{
					plainText = toPlatformString((const char*)text->data + offset, i - offset);

				}
				
				auto end = i;
				for(;end - i < 5 && end < text->size && text->data[end] != 4;end++);

				std::array<uint8_t, 6> atoiBuff;
				memset(&atoiBuff[0], 0, 6);
				memcpy(&atoiBuff[0], text->data + i + 1, end - i - 1);
				offset = i + 6;
				return atoi((const char*)&atoiBuff[0]);
			}
		}
		plainText = nullptr;
		return 0;
	}

	//this is for anywhere we are making new Text objects, 
	void splat_text(const struct buf * text, dom_builder_state* state, vector<uint32_t>& splatIds)
	{
		//find naked text between ids
		ptrdiff_t offset = 0;
		vector<uint32_t> bufferIds;
		while(offset < text->size)
		{
			string plainText = nullptr;
			auto foundId = nextId(text, offset, plainText);
			if(plainText.size() > 0)
			{
				auto newDomId = state->domId++;
				state->unclaimedDomIdMap[newDomId] = state->memoryPool.make_new<Text>(plainText, newDomId);
				splatIds.push_back(newDomId);
			}

			if(foundId != 0)
				splatIds.push_back(foundId);
			else
			{
				auto newDomId = state->domId++;
				state->unclaimedDomIdMap[newDomId] = state->memoryPool.make_new<Text>(toPlatformString((const char*)text->data + offset, text->size - offset), newDomId);
				splatIds.push_back(newDomId);
				break;
			}
		}
	}

	void consume_text(const struct buf* text, dom_builder_state* state, vector<IDomObject*>& expanded)
	{
		//find naked text between ids
		ptrdiff_t offset = 0;
		vector<uint32_t> bufferIds;
		while(offset < text->size)
		{
			std::string plainText = nullptr;
			auto foundId = nextId(text, offset, plainText);
			if(plainText.size() > 0)
			{
				auto newDomId = state->domId++;
				expanded.push_back(state->memoryPool.make_new<Text>(plainText, newDomId));
			}

			if(foundId != 0)
			{
				auto findItr = state->unclaimedDomIdMap.find(foundId);
				expanded.push_back(findItr->second);
				state->unclaimedDomIdMap.erase(findItr);
			}
			else
			{
				auto newDomId = state->domId++;
				expanded.push_back(state->memoryPool.make_new<Text>(toPlatformString((const char*)text->data + offset, text->size - offset), newDomId));
				break;
			}
		}
	}

	static int rndr_autolink(struct buf *ob, const struct buf *link, enum mkd_autolink type, void *opaque) 
	{
		
		if (!link || !link->size) return 0;

		auto state = static_cast<dom_builder_state*>(opaque);
		//children should not have any elements
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<Link>(toPlatformString(link), nullptr, vector<IDomObject*>(), newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
		return 1; 
	}

	static void rndr_blockcode(struct buf *ob, const struct buf *text, const struct buf *lang, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<Code>(expanded, newDomId);
		result->IsBlock = true;
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	static void rndr_blockquote(struct buf *ob, const struct buf *text, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<Quote>(expanded, state->domId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	static int rndr_codespan(struct buf *ob,const  struct buf *text, void *opaque) 
	{
		if(text != nullptr)
		{
			//we should be looking at zero or more processed children here
			auto state = static_cast<dom_builder_state*>(opaque);
			vector<IDomObject*> expanded;
			consume_text(text, state, expanded);
			auto newDomId = state->domId++;
			auto result = state->memoryPool.make_new<Code>(expanded, newDomId);
			result->IsBlock = false;
			state->unclaimedDomIdMap[newDomId] = result;
			makeDomId(ob, newDomId, opaque);
		}
		return 1;
	}

	static int rndr_triple_emphasis(struct buf *ob, const struct buf *text, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<uint32_t> splatIds;
		splat_text(text, state, splatIds);
		for(auto id : splatIds)
		{
			((Text*)state->unclaimedDomIdMap[id])->Bold = true;
			makeDomId(ob, id, opaque);
		}
		
		return 1;
	}

	static int rndr_double_emphasis(struct buf *ob, const struct buf *text, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<uint32_t> splatIds;
		splat_text(text, state, splatIds);
		for(auto id : splatIds)
		{
			auto textElement = dynamic_cast<Text*>(state->unclaimedDomIdMap[id]);
			if(textElement != nullptr)
				textElement->Bold = true;
			makeDomId(ob, id, opaque);
		}
		return 1;
	}

	static int rndr_emphasis(struct buf *ob, const struct buf *text, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<uint32_t> splatIds;
		splat_text(text, state, splatIds);
		for(auto id : splatIds)
		{
			auto textElement = dynamic_cast<Text*>(state->unclaimedDomIdMap[id]);
			if(textElement != nullptr)
				textElement->Italic = true;
			
			makeDomId(ob, id, opaque);
		}
		return 1;
	}

	static int rndr_strikethrough(struct buf *ob, const struct buf *text, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<uint32_t> splatIds;
		splat_text(text, state, splatIds);
		for(auto id : splatIds)
		{
			auto textElement = dynamic_cast<Text*>(state->unclaimedDomIdMap[id]);
			if(textElement != nullptr)
				textElement->Strike = true;
			makeDomId(ob, id, opaque);
		}
		return 1;
	}

	static int rndr_superscript(struct buf *ob, const struct buf *text, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<uint32_t> splatIds;
		splat_text(text, state, splatIds);
		for(auto id : splatIds)
		{
			auto textElement = dynamic_cast<Text*>(state->unclaimedDomIdMap[id]);
			if(textElement != nullptr)
				textElement->Superscript = true;
			makeDomId(ob, id, opaque);
		}
		return 1;
	}

	static void rndr_header(struct buf *ob, const struct buf *text, int level, void *opaque)
	{
		if(level > 6) level = 4;
		if(level < 1) level = 4;

		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<uint32_t> splatIds;
		splat_text(text, state, splatIds);
		for(auto id : splatIds)
		{
			auto textElement = dynamic_cast<Text*>(state->unclaimedDomIdMap[id]);
			if(textElement != nullptr)
				textElement->HeaderSize = level;
			makeDomId(ob, id, opaque);
		}
	}

	static int rndr_link(struct buf *ob, const struct buf *link, const struct buf *title, const struct buf *content, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expandedObjects;
		if(content != nullptr && content->size > 0)
			consume_text(content, state, expandedObjects);

		auto newDomId = state->domId++;
		auto linkUrl = toPlatformString(link);
		auto result = state->memoryPool.make_new<Link>(linkUrl,
			toPlatformString(title),expandedObjects, newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
		return 1;  
	}

	static void rndr_list(struct buf *ob, const struct buf *text, int flags, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		IDomObject* result;
		auto newDomId = state->domId++;
		if(flags == MKD_LIST_ORDERED)
			result = state->memoryPool.make_new<OrderedList>(expanded, newDomId);
		else
			result = state->memoryPool.make_new<UnorderedList>(expanded, newDomId);

		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	static void rndr_listitem(struct buf *ob, const struct buf *text, int flags, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<Paragraph>(expanded, newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	static void rndr_paragraph(struct buf *ob, const struct buf *text, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<Paragraph>(expanded, newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	static void rndr_hrule(struct buf *ob, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<HorizontalRule>(newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	static int rndr_linebreak(struct buf *ob, void *opaque) 
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<LineBreak>(newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
		
		return 1; 
	}

	void rndr_table(struct buf *ob, const struct buf *header, const struct buf *body, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		if(body != nullptr && body->size != 0)
			consume_text(body, state, expanded);

		vector<IDomObject*> expandedHeader;
		if(header != nullptr && header->size != 0)
			consume_text(header, state, expandedHeader);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<Table>(expandedHeader, expanded, newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	void rndr_table_row(struct buf *ob, const struct buf *text, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<TableRow>(expanded, newDomId);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	void rndr_table_cell(struct buf *ob, const struct buf *text, int flags, void *opaque)
	{
		//we should be looking at zero or more processed children here
		auto state = static_cast<dom_builder_state*>(opaque);
		vector<IDomObject*> expanded;
		consume_text(text, state, expanded);
		auto newDomId = state->domId++;
		auto result = state->memoryPool.make_new<TableColumn>(expanded, newDomId, flags);
		state->unclaimedDomIdMap[newDomId] = result;
		makeDomId(ob, newDomId, opaque);
	}

	void* rndr_allocate(void *opaque, size_t size)
	{
		auto state = static_cast<dom_builder_state*>(opaque);
		return state->memoryPool.alloc(size);
	}

	/* exported renderer structure */
	const struct sd_callbacks mkd_dom = 
	{
		rndr_blockcode,
		rndr_blockquote,
		NULL,
		rndr_header,
		rndr_hrule,
		rndr_list,
		rndr_listitem,
		rndr_paragraph,
		rndr_table,
		rndr_table_row,
		rndr_table_cell,

		rndr_autolink,
		rndr_codespan,
		rndr_double_emphasis,
		rndr_emphasis,
		NULL,
		rndr_linebreak,
		rndr_link,
		NULL,
		rndr_triple_emphasis,
		rndr_strikethrough,
		rndr_superscript,

		NULL,
		NULL,

		NULL,
		NULL,
		rndr_allocate
	};

	static void toBufString(const wchar_t* src, uint32_t srcLength, buf* target, void* opaque, void* (*allocate)(void *opaque, size_t size))
	{
		if(src == nullptr)
		{
			return;
		}
		int length = srcLength * 2;
		bufgrow(opaque, allocate, target, length);
		length = WideCharToMultiByte(CP_UTF8, 0, src, srcLength, (char*)target->data, length, NULL, NULL);
		if(length == -1)
			target->size = 0;
		else
			target->size = length;
	}

	static const unsigned int snudown_default_md_flags =
		MKDEXT_NO_INTRA_EMPHASIS |
		MKDEXT_SUPERSCRIPT |
		MKDEXT_AUTOLINK |
		MKDEXT_STRIKETHROUGH |
		MKDEXT_TABLES;

	Document::Document() : IDomContainer(std::vector<IDomObject*>(), 0), State(new dom_builder_state()) {}
	Document::~Document() {}

	SnooDom::SnooDom() : document(new Document()) {}

	SnooDom^ SnooDom::MarkdownToDOM(Platform::String^ source)
	{
		try
		{
			//when this goes out of scope, all memory consumed by this session will be freed
			auto result = ref new SnooDom();
			buf* g_ib = bufnew(&result->document->State, mkd_dom.allocate, 1024);
			buf* g_ob = bufnew(&result->document->State, mkd_dom.allocate, 1024);
			toBufString(source->Data(), source->Length(), g_ib, &result->document->State, mkd_dom.allocate);

				
			auto markdownProcessor = sd_markdown_new(snudown_default_md_flags, 100, &mkd_dom, &result->document->State);

			sd_markdown_render(g_ob, g_ib->data, g_ib->size, markdownProcessor);
			vector<IDomObject*> topLevelObjects;
			consume_text(g_ob, result->document->State.get(), topLevelObjects);
			return result;
		}
		catch(...)
		{

		}
	return nullptr;
	}

	TableColumn::TableColumn(std::vector<IDomObject*>& children, uint32_t domId, int flags) : IDomContainer(children, domId)
	{
		switch (flags & MKD_TABLE_ALIGNMASK)
		{
		case MKD_TABLE_ALIGN_L:
			Alignment = ColumnAlignment::Left;
			break;
		case MKD_TABLE_ALIGN_R:
			Alignment = ColumnAlignment::Right;
			break;
		case MKD_TABLE_ALIGN_CENTER:
			Alignment = ColumnAlignment::Center;
			break;
		}
	}

	TableRow::TableRow(std::vector<IDomObject*>& children, uint32_t domId) : IDomObject(domId)
	{
		for (auto obj : children)
		{
			auto objCol = dynamic_cast<TableColumn*>(obj);
			if (objCol != nullptr)
				Columns.push_back(objCol);
		}
	}
}