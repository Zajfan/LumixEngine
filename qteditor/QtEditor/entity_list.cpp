#include "entity_list.h"
#include "ui_entity_list.h"
#include "core/crc32.h"
#include "core/path_utils.h"
#include "core/string.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"
#include "universe/entity.h"
#include "universe/hierarchy.h"
#include <qmimedata.h>


static const char* component_map[] =
{
	"Animable", "animable",
	"Camera", "camera",
	"Directional light", "light",
	"Mesh", "renderable",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Script", "script",
	"Terrain", "terrain"
};


static const uint32_t RENDERABLE_HASH = crc32("renderable");


class SetParentEditorCommand : public Lumix::IEditorCommand
{
	public:
		SetParentEditorCommand(Lumix::Hierarchy& hierarchy, const Lumix::Entity& child, const Lumix::Entity& parent)
			: m_new_parent(parent)
			, m_child(child)
			, m_old_parent(hierarchy.getParent(child))
			, m_hierarchy(hierarchy)
		{
		}

		virtual void execute() override
		{
			m_hierarchy.setParent(m_child, m_new_parent);
		}


		virtual void undo() override
		{
			m_hierarchy.setParent(m_child, m_old_parent);
		}


		virtual bool merge(IEditorCommand&) override
		{
			return false;
		}


		virtual uint32_t getType() override
		{
			static const uint32_t hash = crc32("set_entity_parent");
			return hash;
		}


	private:
		Lumix::Entity m_child;
		Lumix::Entity m_new_parent;
		Lumix::Entity m_old_parent;
		Lumix::Hierarchy& m_hierarchy;
};


class EntityListFilter : public QSortFilterProxyModel
{
	public:
		EntityListFilter(QWidget* parent) : QSortFilterProxyModel(parent), m_component(0), m_is_update_enabled(true) {}
		void filterComponent(uint32_t component) { m_component = component; }
		void setUniverse(Lumix::Universe* universe) { m_universe = universe; invalidate(); }
		void setWorldEditor(Lumix::WorldEditor& editor)
		{
			editor.entityNameSet().bind<EntityListFilter, &EntityListFilter::onEntityNameSet>(this);
		}
		void enableUpdate(bool enable)
		{
			m_is_update_enabled = enable;
		}

	protected:
		virtual bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override
		{
			QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
			if (m_component == 0)
			{
				return sourceModel()->data(index).toString().contains(filterRegExp());
			}
			int entity_index = sourceModel()->data(index, Qt::UserRole).toInt();
			return Lumix::Entity(m_universe, entity_index).getComponent(m_component).isValid() && sourceModel()->data(index).toString().contains(filterRegExp());
		}

		void onEntityNameSet(const Lumix::Entity&, const char*)
		{
			if (m_is_update_enabled)
			{
				invalidate();
			}
		}

	private:
		uint32_t m_component;
		Lumix::Universe* m_universe;
		bool m_is_update_enabled;
};


class EntityListModel : public QAbstractItemModel
{
	private:
		class EntityNode
		{
			public:
				EntityNode(EntityNode* parent, const Lumix::Entity& entity) : m_entity(entity), m_parent(parent) {}

				~EntityNode()
				{
					for(int i = 0; i < m_children.size(); ++i)
					{
						delete m_children[i];
					}
				}

				EntityNode* getNode(const Lumix::Entity& entity)
				{
					if(m_entity == entity)
					{
						return this;
					}
					for(int i = 0; i < m_children.size(); ++i)
					{
						EntityNode* node = m_children[i]->getNode(entity);
						if(node)
						{
							return node;
						}
					}
					return NULL;
				}

				bool removeEntity(const Lumix::Entity& entity)
				{
					if(m_entity == entity)
					{
						return true;
					}
					for(int i = 0; i < m_children.size(); ++i)
					{
						if(m_children[i]->removeEntity(entity))
						{
							m_children.erase(i);
							return false;
						}
					}
					return false;
				}

				EntityNode* m_parent;
				Lumix::Entity m_entity;
				Lumix::Array<EntityNode*> m_children;
		};

	public:
		EntityListModel(QWidget* parent, EntityListFilter* filter)
			: QAbstractItemModel(parent)
		{
			m_root = NULL;
			m_universe = NULL;
			m_filter = filter;
			m_is_update_enabled = true;
		}


		void enableUpdate(bool enable)
		{
			m_is_update_enabled = enable;
		}


		virtual Qt::ItemFlags flags(const QModelIndex &index) const override
		{
			Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);

			if (index.isValid())
			{
				return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | defaultFlags;
			}
			else
			{
				return Qt::ItemIsDropEnabled | defaultFlags;
			}
		}


		virtual bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override
		{
			if (action == Qt::IgnoreAction)
			{
				return true;
			}
			if (!data->hasFormat("application/lumix.entity"))
			{
				return false;
			}
			if (column > 0)
			{
				return false;
			}

			Lumix::Entity parent_entity(m_universe, -1);
			if (row != -1)
			{
				parent_entity.index = parent.data(Qt::UserRole).toInt();
			}
			else if (parent.isValid())
			{
				parent_entity.index = parent.data(Qt::UserRole).toInt();
			}

			QByteArray encodedData = data->data("application/lumix.entity");
			QDataStream stream(&encodedData, QIODevice::ReadOnly);
			QStringList newItems;

			Lumix::Entity child(m_universe, -1);
			if (!stream.atEnd()) 
			{
				stream >> child.index;
			}

			SetParentEditorCommand* command = new (Lumix::dll_lumix_new(sizeof(SetParentEditorCommand), "", 0)) SetParentEditorCommand(*m_engine->getHierarchy(), child, parent_entity);
			m_engine->getWorldEditor()->executeCommand(command);

			return false;
		}


		virtual Qt::DropActions supportedDropActions() const override
		{
			return Qt::CopyAction;
		}


		virtual QMimeData* mimeData(const QModelIndexList &indexes) const override
		{
			QMimeData *mimeData = new QMimeData();
			QByteArray encodedData;

			QDataStream stream(&encodedData, QIODevice::WriteOnly);

			stream << indexes.first().data(Qt::UserRole).toInt();

			mimeData->setData("application/lumix.entity", encodedData);
			return mimeData;
		}


		virtual QStringList mimeTypes() const override
		{
			QStringList types;
			types << "application/lumix.entity";
			return types;
		}


		virtual QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override
		{
			if(role == Qt::DisplayRole)
			{
				switch(section)
				{
					case 0:
						return "ID";
						break;
					default:
						ASSERT(false);
						return QVariant();
				}
			}
			return QVariant();
		}
		
		
		virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override
		{
			if (!hasIndex(row, column, parent))
			{
				return QModelIndex();
			}

			EntityNode *parentItem;

			if (!parent.isValid())
			{
				parentItem = m_root;
			}
			else
			{
				parentItem = static_cast<EntityNode*>(parent.internalPointer());
			}

			EntityNode *childItem = parentItem->m_children[row];
			if (childItem)
			{
				return createIndex(row, column, childItem);
			}
			return QModelIndex();
		}
		
		
		virtual QModelIndex parent(const QModelIndex& index) const override
		{
			if (!index.isValid() || !m_root)
			{
				return QModelIndex();
			}

			EntityNode *childItem = static_cast<EntityNode*>(index.internalPointer());
			EntityNode *parentItem = childItem->m_parent;

			if (parentItem == m_root)
			{
				return QModelIndex();
			}

			int row = parentItem->m_parent->m_children.indexOf(parentItem);
			return createIndex(row, 0, parentItem);
		}
		
		
		virtual int rowCount(const QModelIndex& parent) const override
		{
			if (parent.column() > 0 || !m_root)
			{
				return 0;
			}

			if (!parent.isValid())
			{
				return m_root->m_children.size();
			}
			EntityNode* node = static_cast<EntityNode*>(parent.internalPointer());
			return node->m_children.size();
		}
		
		
		virtual int columnCount(const QModelIndex&) const override
		{
			return 1;
		}


		virtual QVariant data(const QModelIndex& index, int role) const override
		{
			if (!index.isValid())
			{
				 return QVariant("X");
			}

			EntityNode *item = static_cast<EntityNode*>(index.internalPointer());

			if (index.isValid() && role == Qt::DisplayRole)
			{
				Lumix::Component renderable = item->m_entity.getComponent(RENDERABLE_HASH);
				if (renderable.isValid())
				{
					Lumix::string path;
					static_cast<Lumix::RenderScene*>(renderable.scene)->getRenderablePath(renderable, path);
					const char* name = item->m_entity.getName();
					char basename[LUMIX_MAX_PATH];
					Lumix::PathUtils::getBasename(basename, LUMIX_MAX_PATH, path.c_str());
					return name && name[0] != '\0' ? QVariant(QString("%1 - %2").arg(name).arg(basename)) : QVariant(QString("%1 - %2").arg(item->m_entity.index).arg(basename));
				}
				const char* name = item->m_entity.getName();
				return name && name[0] != '\0' ? QVariant(name) : QVariant(item->m_entity.index);
			}
			else if (index.isValid() && role == Qt::UserRole)
			{
				return item->m_entity.index;
			}
			return role == 6 ? QVariant(QString("AAA")) : QVariant();
		}


		void setEngine(Lumix::Engine& engine)
		{
			m_engine = &engine;
		}


		void fillChildren(EntityNode* node)
		{
			Lumix::Array<Lumix::Hierarchy::Child>* children = m_engine->getHierarchy()->getChildren(node->m_entity);
			if(children)
			{
				for(int i = 0; i < children->size(); ++i)
				{
					EntityNode* new_node = new EntityNode(node, Lumix::Entity(m_universe, (*children)[i].m_entity));
					node->m_children.push(new_node);
					fillChildren(new_node);
				}
			}
		}


		void onParentSet(const Lumix::Entity& child, const Lumix::Entity& parent)
		{
			if(!m_root->m_children.empty())
			{
				EntityNode* node = m_root->getNode(child);
				node->m_parent->m_children.eraseItem(node);

				EntityNode* parent_node = m_root->getNode(parent);
				if(!parent_node)
				{
					parent_node = m_root;
				}
				parent_node->m_children.push(node);
				node->m_parent = parent_node;

				if (m_is_update_enabled)
				{
					m_filter->invalidate();
				}
			}
		}


		void setUniverse(Lumix::Universe* universe)
		{
			m_filter->setUniverse(universe);
			if(m_universe)
			{
				m_universe->entityCreated().unbind<EntityListModel, &EntityListModel::onEntityCreated>(this);
				m_universe->entityDestroyed().unbind<EntityListModel, &EntityListModel::onEntityDestroyed>(this);
			}
			delete m_root;
			m_root = new EntityNode(NULL, Lumix::Entity::INVALID);
			m_universe = universe;
			if(m_universe)
			{
				m_engine->getHierarchy()->parentSet().bind<EntityListModel, &EntityListModel::onParentSet>(this);
				m_universe->entityCreated().bind<EntityListModel, &EntityListModel::onEntityCreated>(this);
				m_universe->entityDestroyed().bind<EntityListModel, &EntityListModel::onEntityDestroyed>(this);
				Lumix::Entity e = m_universe->getFirstEntity();
				while(e.isValid())
				{
					Lumix::Entity parent = m_engine->getHierarchy()->getParent(e);
					if(!parent.isValid())
					{
						EntityNode* node = new EntityNode(m_root, e);
						m_root->m_children.push(node);
						fillChildren(node);
					}
					e = m_universe->getNextEntity(e);
				}
			}
			if (m_universe && !m_root->m_children.empty() && m_is_update_enabled)
			{
				m_filter->invalidate();
			}
		}

	
	private:
		void onEntityCreated(const Lumix::Entity& entity)
		{
			EntityNode* node = new EntityNode(m_root, entity);
			m_root->m_children.push(node);
			if (m_is_update_enabled)
			{
				m_filter->invalidate();
			}
		}

		void onEntityDestroyed(const Lumix::Entity& entity)
		{
			m_root->removeEntity(entity);
			if (m_is_update_enabled)
			{
				m_filter->invalidate();
			}
		}

	private:
		Lumix::Universe* m_universe;
		Lumix::Engine* m_engine;
		EntityNode* m_root;
		EntityListFilter* m_filter;
		bool m_is_update_enabled;
};


EntityList::EntityList(QWidget *parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::EntityList)
{
	m_is_update_enabled = true;
	m_universe = NULL;
	m_ui->setupUi(this);
	m_filter = new EntityListFilter(this);
	m_model = new EntityListModel(this, m_filter);
	m_filter->setDynamicSortFilter(true);
	m_filter->setSourceModel(m_model);
	m_ui->entityList->setModel(m_filter);
	m_ui->entityList->setDragEnabled(true);
	m_ui->entityList->setAcceptDrops(true);
	m_ui->entityList->setDropIndicatorShown(true);
}


EntityList::~EntityList()
{
	m_editor->universeCreated().unbind<EntityList, &EntityList::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<EntityList, &EntityList::onUniverseDestroyed>(this);
	m_editor->universeLoaded().unbind<EntityList, &EntityList::onUniverseLoaded>(this);
	m_editor->entitySelected().unbind<EntityList, &EntityList::onEntitySelected>(this);

	delete m_ui;
}


void EntityList::enableUpdate(bool enable)
{
	m_is_update_enabled = enable;
	m_filter->enableUpdate(enable);
	m_model->enableUpdate(enable);
	m_filter->invalidate();
}


void EntityList::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	editor.universeCreated().bind<EntityList, &EntityList::onUniverseCreated>(this);
	editor.universeDestroyed().bind<EntityList, &EntityList::onUniverseDestroyed>(this);
	editor.universeLoaded().bind<EntityList, &EntityList::onUniverseLoaded>(this);
	m_universe = editor.getEngine().getUniverse();
	m_model->setEngine(editor.getEngine());
	m_model->setUniverse(m_universe);
	m_filter->setSourceModel(m_model);
	m_filter->setWorldEditor(editor);
	m_ui->comboBox->clear();
	m_ui->comboBox->addItem("All");
	for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		m_ui->comboBox->addItem(component_map[i]);
	}
	editor.entitySelected().bind<EntityList, &EntityList::onEntitySelected>(this);
}


void EntityList::onEntitySelected(const Lumix::Array<Lumix::Entity>& entities)
{
	QItemSelection* selection = new QItemSelection();
	for(int j = entities.size() - 1; j >= 0; --j)
	{
		for (int i = 0, c = m_filter->rowCount(); i < c; ++i)
		{
			if (m_filter->data(m_filter->index(i, 0), Qt::UserRole).toInt() == entities[j].index)
			{
				selection->append(QItemSelectionRange(m_filter->index(i, 0)));
				break;
			}
		}
	}
	m_ui->entityList->selectionModel()->select(*selection, QItemSelectionModel::SelectionFlag::ClearAndSelect | QItemSelectionModel::SelectionFlag::Rows);
	delete selection;
}


void EntityList::onUniverseCreated()
{
	m_universe = m_editor->getEngine().getUniverse();
	m_model->setUniverse(m_universe);
}


void EntityList::onUniverseLoaded()
{
	m_universe = m_editor->getEngine().getUniverse();
	m_model->setUniverse(m_universe);
	if (m_is_update_enabled)
	{
		m_filter->invalidate();
	}
}


void EntityList::onUniverseDestroyed()
{
	m_model->setUniverse(NULL);
	m_universe = NULL;
}


void EntityList::on_entityList_clicked(const QModelIndex &index)
{
	m_editor->selectEntities(&Lumix::Entity(m_universe, m_filter->data(index, Qt::UserRole).toInt()), 1);
}


void EntityList::on_comboBox_activated(const QString &arg1)
{
	for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if (arg1 == component_map[i])
		{
			m_filter->filterComponent(crc32(component_map[i + 1]));
			if (m_is_update_enabled)
			{
				m_filter->invalidate();
			}
			return;
		}
	}
	m_filter->filterComponent(0);
	if (m_is_update_enabled)
	{
		m_filter->invalidate();
	}
}

void EntityList::on_nameFilterEdit_textChanged(const QString &arg1)
{
	QRegExp regExp(arg1);
	m_filter->setFilterRegExp(regExp);
}
