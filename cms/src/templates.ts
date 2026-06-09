import { randomUUID } from 'crypto';
import { JsonStore } from './store';

// A reusable, named command — the unit of the central template library. Deploying
// a template to a set of devices simply fans its command out to each. A "config
// template" is one whose command is apply_config; a "profile template" is one
// whose command is activate_profile.
export interface Template {
  id: string;
  name: string;
  description?: string;
  command: { type: string; payload?: unknown };
  updatedAt: number;
}

export interface TemplateInput {
  id?: string;
  name?: string;
  description?: string;
  command?: { type?: string; payload?: unknown };
}

export class TemplateStore {
  private readonly templates = new Map<string, Template>();

  constructor(private readonly store: JsonStore<{ templates: Template[] }>) {
    const saved = store.load();
    if (saved?.templates) {
      for (const t of saved.templates) this.templates.set(t.id, t);
    }
  }

  list(): Template[] {
    return [...this.templates.values()].sort((a, b) => a.name.localeCompare(b.name));
  }

  get(id: string): Template | undefined {
    return this.templates.get(id);
  }

  /** Create or update. Returns the saved template, or null if input is invalid. */
  upsert(input: TemplateInput): Template | null {
    const name = typeof input.name === 'string' ? input.name.trim() : '';
    const type = typeof input.command?.type === 'string' ? input.command.type.trim() : '';
    if (!name || !type) return null;

    const id = input.id && this.templates.has(input.id) ? input.id : randomUUID();
    const template: Template = {
      id,
      name,
      description: typeof input.description === 'string' ? input.description : undefined,
      command: { type, payload: input.command?.payload ?? null },
      updatedAt: Date.now(),
    };
    this.templates.set(id, template);
    this.persist();
    return template;
  }

  remove(id: string): boolean {
    const ok = this.templates.delete(id);
    if (ok) this.persist();
    return ok;
  }

  private persist(): void {
    this.store.saveNow({ templates: this.list() });
  }
}
